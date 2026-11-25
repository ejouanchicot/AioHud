// minimap.cpp -- see minimap.h. Phase 1a placeholder : a square panel showing the 512px map bounds as a
// frame, with the player dot + heading arrow placed by the reversed transform. Validates the world->pixel
// math live before we extract the real zone map image (phase 1b).
#include "minimap.h"
#include "perf.h"                    // PERF_SCOPE : the Dial's parts in //aio perf
#include <string.h>                  // memcmp : the mesh caches' keys
#include "model/paths.h"
#include "edit_box.h"                // shared edit-mode drag (Target/Player/Minimap)
#include "model/gamestate.h"
#include "model/ui_config.h"
#include "model/party_state.h"       // party() : self id + roster (marker colour by claim/party, like the Target box)
#include "model/map_dat.h"           // load_zone_map (ROM DAT extraction)
#include "model/game_mem.h"          // current_submap : logged when a map load fails (black-minimap diagnosis)
#include "windower_debug.h"          // MAP FAIL log (always on -- the bug is rare and can't be armed for)
#include "retry_clock.h"             // retry_due / retry_arm : bounded retry with a delay, the project's one idiom
#include "model/model_clock.h"       // model_now_ms : the Wide Scan track freshness (same clock as the 0x0F5 stamp)
#include "model/selftest.h"          // MAP.LOAD_FAILED : a map that will not load reaches doctor and the watcher

// The marker/element retry budget. 12 attempts over 2.4 s was the old value and it was too short to survive a
// slow zone-in ; these give about a minute, which is the order of the project's other budgets.
static const int      MK_TRIES    = 24;
static const unsigned MK_RETRY_MS = 2500;
#include "gfx/draw.h"
#include "ui/tune.h"                // TUNE : the marker level-of-detail knobs (dev : data\tune.txt)
#include "gfx/font.h"
#include "gfx/texture.h"             // make_texture_argb_mip / release_texture
#include "gfx/window.h"              // window_theme_family / window_theme_variant / box_hue_color (frame theme)
#include "gfx/d3d.h"                 // dSet* + FVF constants
#include "ui/text_style.h"           // te_sz/te_ow/te_col : shared TextStyle-resolve impl
#include "ui/entity_color.h"         // in_my_group / allegiance_color : shared claim/allegiance palette (with the Target box)
#include "ui/box_style.h"            // draw_themed_box : shared themed chrome for the clock box
#include <math.h>
#include <stdio.h>
#include <time.h>

namespace aio {

// ---- the harness : a map that will not load, said where someone looks ----------------------------------------
// A zone whose map fails to load used to leave one MAP FAIL line in aiohud_debug.log and an empty panel -- nothing
// in //aio doctor, nothing in the watcher. The failure is recorded here when the load drops into the slow lane (a
// real failure by then, not the normal not-ready-yet after a zone-in) and cleared the moment a map texture exists.
static struct MapFailNote {
    bool     on;
    unsigned zone, fileId, fileSize, biSize, bpp;
    int      step, W, H;
    bool     overlay, texFailed;
} s_mapFail;

static int minimap_checks(CheckFail* out, int cap) {
    if (!s_mapFail.on || cap <= 0) return 0;
    static const char* STEP[8] = { "OK", "NO FFXI ROOT", "PATH UNRESOLVED", "FILE UNREADABLE", "NO GRAPHIC CHUNK", "FORMAT REJECTED", "?", "?" };
    lstrcpynA(out[0].id, "MAP.LOAD_FAILED", sizeof(out[0].id));
    out[0].sev = CHK_WARN;
    if (s_mapFail.texFailed)
        _snprintf(out[0].detail, sizeof(out[0].detail),
                  "zone %u map 0x%04X decoded (%dx%d) but CreateTexture failed -- the minimap is empty ; still retried every 15 s",
                  s_mapFail.zone, s_mapFail.fileId, s_mapFail.W, s_mapFail.H);
    else
        _snprintf(out[0].detail, sizeof(out[0].detail),
                  "zone %u map 0x%04X : %s (%u bytes, %dx%d, header %u / %u bpp, overlay=%d) -- the minimap is empty ; MAP FAIL in aiohud_debug.log",
                  s_mapFail.zone, s_mapFail.fileId, STEP[s_mapFail.step & 7], s_mapFail.fileSize, s_mapFail.W, s_mapFail.H,
                  s_mapFail.biSize, s_mapFail.bpp, s_mapFail.overlay ? 1 : 0);
    out[0].detail[sizeof(out[0].detail) - 1] = 0;
    return 1;
}
void minimap_register_checks() { selftest_add("minimap", minimap_checks); }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// darken a colour toward black by fraction f (0..1), keeping its alpha -> a crisp AA outline for the round rim.
static inline u32 mm_darken(u32 c, float f) {
    const int rr = (int)(((c >> 16) & 0xFF) * (1.0f - f)), gg = (int)(((c >> 8) & 0xFF) * (1.0f - f)), bb = (int)((c & 0xFF) * (1.0f - f));
    return (c & 0xFF000000u) | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
}

// marker icons (white, tinted per entity) : Location pin (points DOWN) for the player, Arrow (points UP) for mobs.
static const char* MK_PLAYER_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\icon_marker_player.raw"); return b; }
// the v3 marker atlas (scripts/gen_map_markers.py) : 8 x 4 cells of 64 px in a 512 x 256 texture
static const char* MK_ATLAS_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\map_markers.raw"); return b; }
enum { MK_ME_FILL, MK_ME_FRAME, MK_MOB_FILL, MK_MOB_FRAME, MK_NM_FRAME, MK_PARTY_FILL, MK_PARTY_FRAME, MK_ALLY_FILL, MK_ALLY_FRAME,
       MK_TRUST_FILL, MK_TRUST_FRAME, MK_PET_FILL, MK_PET_FRAME, MK_TARGET_FILL, MK_TARGET_FRAME, MK_PIN_FILL, MK_PIN_FRAME,
       MK_PC_OTHER, MK_NPC, MK_HOMEPOINT, MK_GUIDE, MK_MOOGLE, MK_WAYPOINT, MK_QUEST, MK_WIDESCAN, MK_OFFMAP };
// WHAT an entity is drawn as (icon cells, colours, sizes, its class threshold factor). ONE function for the widget
// and the Help sample, so the two can never drift. false = this kind is hidden by the user's toggles.
//   cf : x the zoom threshold (0 = always the icon -- a landmark ; < 0 = always a dot -- a plain NPC)
struct MkClass { int fill, frame, prio; u32 col, frameCol; float cf, isz, dr; bool ring, dir; };
static bool mk_classify(const MapEntity& me, const UiConfig& c, MkClass& k) {
    k.fill = -1; k.frame = -1; k.col = 0xFFFFFFFFu; k.frameCol = 0xFFFFFFFFu; k.ring = false; k.dir = false;
    if (me.kind == MAPK_PET) {
        if (!c.mmPC) return false;
        k.prio = 5; k.cf = -1.0f; k.isz = 13.0f; k.dr = 2.2f; k.fill = MK_PET_FILL; k.frame = MK_PET_FRAME; k.col = 0xFF4FD1C5u; k.ring = true;
    } else if (me.type == 3) {
        if (!c.mmMob) return false;
        const bool nm = (me.kind == MAPK_NM);
        const bool other = (me.claimId != 0 && !in_my_group(me.claimId)), mine = !other && (me.claimId != 0 || me.status == 1);
        k.col = other ? 0xFFE86BE0u : mine ? 0xFFFF5A4Eu : 0xFFFFE600u;
        k.prio = nm ? 5 : (mine ? 4 : 3); k.cf = nm ? 0.5f : 1.0f; k.isz = nm ? 18.0f : 13.0f; k.dr = nm ? 3.2f : 2.2f;
        k.fill = MK_MOB_FILL; k.frame = MK_MOB_FRAME; k.frameCol = nm ? 0xFFFFD34Au : 0xFF4A3A22u;   // an NM : the same arrow, bigger, a BRIGHT gold frame (the crown was unreadable at 16 px)   // mobs : a dark bronze frame (any fill pops, never read as a player)
        k.dir = true;
    } else if (me.type == 2) {
        if (me.kind == MAPK_TRUST) {
            if (!c.mmPC) return false;
            k.prio = 5; k.cf = -1.0f; k.isz = 14.0f; k.dr = 2.2f; k.fill = MK_TRUST_FILL; k.frame = MK_TRUST_FRAME; k.col = 0xFF9CCBFFu; k.ring = true;
        } else {
            if (!c.mmNPC) return false;
            static const int SVC[9] = { -1, -1, -1, -1, MK_HOMEPOINT, MK_GUIDE, MK_MOOGLE, MK_WAYPOINT, MK_QUEST };   // by MapKind
            const int svc = (me.kind < 9) ? SVC[me.kind] : -1;
            if (svc >= 0) { k.prio = 9; k.cf = 0.0f; k.isz = 24.0f; k.dr = 3.0f; k.fill = svc; }   // a LANDMARK : always the icon
            else { k.prio = 1; k.cf = -1.0f; k.isz = 0.0f; k.dr = 1.6f; k.col = 0xFF5ED37Cu; }    // a plain NPC : always a dot
        }
    } else {
        if (!c.mmPC) return false;
        if (me.spawn & 0x04)      { k.prio = 7; k.cf = -1.0f; k.isz = 15.0f; k.dr = 2.2f; k.fill = MK_PARTY_FILL; k.frame = MK_PARTY_FRAME; k.col = 0xFF5AB4FFu; k.ring = true; }
        else if (me.spawn & 0x08) { k.prio = 6; k.cf = -1.0f; k.isz = 14.0f; k.dr = 2.2f; k.fill = MK_ALLY_FILL;  k.frame = MK_ALLY_FRAME;  k.col = 0xFFB48CFFu; k.ring = true; }
        else                      { k.prio = 2; k.cf = -1.0f; k.isz = 9.0f;  k.dr = 2.2f; k.fill = MK_PC_OTHER; k.col = 0xFFD8DEE6u; }
    }
    return true;
}
// the textures the LIVE widget holds (same device), lent to the Help sample : the icon atlas and the Astral kit.
// Set by Minimap::draw each frame they exist, forgotten with the widget's own on device-lost / dispose.
static u32 s_shAtlas = 0;
static u32 s_shAst[AS_N] = {};

struct GroupDot { float x, y, r, a; u32 col; };
static GroupDot s_group[64];                                   // this frame's group dots (drawn after the player arrow)
static int s_groupN = 0;
static void mk_cell(u32 dev, int cell, float cx, float cy, float sz, float ang, u32 col) {   // the atlas must be bound
    const float u0 = (float)(cell % 8) / 8.0f, v0 = (float)(cell / 8) / 4.0f;
    tquad_rot_uv(dev, snap(cx), snap(cy), sz, sz, ang, u0, v0, u0 + 1.0f / 8.0f, v0 + 1.0f / 4.0f, col);
}
static const char* MK_MOB_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\icon_marker_mob.raw"); return b; }
static const char* ELEM_ATLAS_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\element_icons.raw"); return b; }

// the shared colour-quad render state (untextured diffuse quads) -- mirrors player/target setup.
static void color_state(u32 dev) {
    dColorQuadState(dev);   // shared 2D colour-quad pipeline (gfx/d3d.h)
}

// textured-quad render state for the map image draw.
static void tex_state(u32 dev, u32 tex) { dTexQuadState(dev, tex, true, true); }   // LINEAR mips (crisp minified map) + BORDER (no edge smear)
// marker colour, matching the Target box's name-colour convention (target.cpp target_name_color) :
// NPC/object green ; mob unclaimed gold / your-claim|engaged red / other-claim magenta ; PC party cyan /
// another-party blue / solo white.
// marker colour = the shared claim/allegiance palette (ui/entity_color.h), keyed off the precomputed type
// (3 mob / 1 PC ; unclaimed mob -> gold). Self is excluded from the markers so its self->white branch never fires.
static u32 mm_ent_color(const MapEntity& me) {
    return allegiance_color(me.type == 3, me.type == 1, me.id, me.claimId, me.status, me.pflags, 0xFFF2F2B7u, me.spawn);
}

// frame colour : follow the party/alliance box theme (procedural hue / FFXI skin border) or a custom colour.
static u32 mm_frame_color(const UiConfig& c, const Frame& f) {
    if (c.mmFrame == 2) return c.mmFrameColor | 0xFF000000u;               // custom
    const int th = c.skinTheme;                                            // follow the party/alliance box theme
    if (window_theme_family(th) == 0) return (f.skin && f.skin->ready()) ? f.skin->borderColor : 0xFF6699BBu;
    return box_theme_border_color(th, c.skinHue);                          // procedural : its REAL frame colour (Royal gold, ...) ; custom hue if set
}

static EditBox g_mmEdit;   // shared edit-mode drag state for the Minimap box

Minimap::Minimap(const GameState* state) : state_(state) { px_ = 40.0f; py_ = 120.0f; }

// eased facing angle for a mob : lerp the stored angle toward `target` along the shortest arc (handles the
// -pi..pi wrap) so the arrow turns smoothly. New ids start at the target ; the cache is reset on a zone change.
static float mm_wrap_pi(float a) { while (a > 3.14159265f) a -= 6.28318530f; while (a < -3.14159265f) a += 6.28318530f; return a; }
float Minimap::eased_angle(unsigned id, float target) {
    for (int i = 0; i < mobAngN_; ++i) if (mobAng_[i].id == id) {
        mobAng_[i].ang += mm_wrap_pi(target - mobAng_[i].ang) * 0.20f;
        return mobAng_[i].ang;
    }
    if (mobAngN_ < 256) { mobAng_[mobAngN_].id = id; mobAng_[mobAngN_].ang = target; return mobAng_[mobAngN_++].ang; }
    return target;
}

// ---- Vana'diel clock header (top of the box) : big time, elemental day icon + name -> next day, moon %, real/GMT.
static const char* VANA_DAYNAME[8] = { "Firesday", "Earthsday", "Watersday", "Windsday", "Iceday", "Lightningday", "Lightsday", "Darksday" };
static const u32   VANA_DAYCOL[8]  = { 0xFFFF7A4Au, 0xFFD8B06Au, 0xFF5AA6FFu, 0xFF74E08Au, 0xFFA6E0FFu, 0xFFC79BFFu, 0xFFFFF2C0u, 0xFFB07CFFu };  // Fire,Earth,Water,Wind,Ice,Lightning,Light,Dark
static const char* moon_phase_name(int id) {   // FFXI 12-phase id -> name (the name is set by the phase id, NOT the %)
    switch (id) {
        case 0:         return "New Moon";
        case 1: case 2: return "Waxing Crescent";
        case 3:         return "First Quarter";
        case 4: case 5: return "Waxing Gibbous";
        case 6:         return "Full Moon";
        case 7: case 8: return "Waning Gibbous";
        case 9:         return "Last Quarter";
        default:        return "Waning Crescent";   // 10, 11
    }
}
// localized (FR) elemental-day + moon-phase names -> the widget clock respects ui_config().lang (was EN-only).
// Defined here (not lower) so both the measure + draw clock passes can see them ; the Help reuses them too.
static const char* moon_phase_name_fr(int id) {   // FFXI 12-phase id -> nom (le nom vient de l'id, PAS du %)
    switch (id) {
        case 0:         return "Nouvelle lune";
        case 1: case 2: return "Croissant montant";
        case 3:         return "Premier quartier";
        case 4: case 5: return "Gibbeuse croissante";
        case 6:         return "Pleine lune";
        case 7: case 8: return "Gibbeuse d\xC3\xA9""croissante";
        case 9:         return "Dernier quartier";
        default:        return "Croissant d\xC3\xA9""croissant";   // 10, 11
    }
}
static const char* VANA_DAYNAME_FR[8] = {
    "Jour de Feu", "Jour de Terre", "Jour d'Eau", "Jour de Vent",
    "Jour de Glace", "Jour de Foudre", "Jour de Lumi\xC3\xA8re", "Jour de T\xC3\xA9n\xC3\xA8""bres"
};
// build a SUPERSAMPLED moon-phase texture (N x N, A8R8G8B8) : day-tinted lit disc + fixed craters + subtle
// top-light + the phase shadow (terminator = ellipse xt = xr*(1-2f)). 3x3 supersampling anti-aliases the disc
// edge AND the terminator -> no stair-stepping. Rebuilt only when the day/phase changes (see Minimap::draw).
static void build_moon_argb(u32* out, int N, u32 lit, float f, bool waning) {
    if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
    const int lr = (lit >> 16) & 0xFF, lg = (lit >> 8) & 0xFF, lb = lit & 0xFF;
    const int sr = 0x0A, sg = 0x0E, sb = 0x22;                                   // shadow = box-indigo dark
    static const float CR[3][3] = { { -0.30f, -0.28f, 0.20f }, { 0.24f, 0.16f, 0.15f }, { -0.12f, 0.30f, 0.11f } };
    const int SS = 3;
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        int cov = 0, rs = 0, gs = 0, bs = 0;
        for (int sy = 0; sy < SS; ++sy) for (int sx = 0; sx < SS; ++sx) {
            const float fx = ((x + (sx + 0.5f) / SS) / (float)N) * 2.0f - 1.0f;
            const float fy = ((y + (sy + 0.5f) / SS) / (float)N) * 2.0f - 1.0f;
            if (fx * fx + fy * fy > 0.9985f) continue;                            // outside the disc -> transparent
            ++cov;
            const float xr = sqrtf(1.0f - fy * fy), xt = xr * (1.0f - 2.0f * f);
            if (waning ? (fx < -xt) : (fx > xt)) {                               // LIT (waxing lit right of terminator)
                float k = 0.94f + 0.12f * (-fy);                                 // subtle top-lighting
                for (int i = 0; i < 3; ++i) { const float dx = fx - CR[i][0], dy = fy - CR[i][1]; if (dx * dx + dy * dy < CR[i][2] * CR[i][2]) k *= 0.80f; }
                if (k > 1.0f) k = 1.0f; if (k < 0.45f) k = 0.45f;
                rs += (int)(lr * k); gs += (int)(lg * k); bs += (int)(lb * k);
            } else { rs += sr; gs += sg; bs += sb; }
        }
        if (!cov) { out[y * N + x] = 0; continue; }
        const u32 a = (u32)(255 * cov / (SS * SS));
        out[y * N + x] = (a << 24) | ((u32)(rs / cov) << 16) | ((u32)(gs / cov) << 8) | (u32)(bs / cov);
    }
}
// draw the pre-built moon texture at radius r + a faint rim.
static void draw_moon(u32 dev, float mcx, float mcy, float r, u32 moonTex) {
    if (!moonTex) return;
    tex_state(dev, moonTex);
    tquad(dev, snap(mcx - r), snap(mcy - r), snap(2.0f * r), snap(2.0f * r), 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
    dSetTex(dev, 0, 0);
    color_state(dev);
    rrect_stroke(dev, mcx - r, mcy - r, 2.0f * r, 2.0f * r, r, 0x40BECDFFu, 1.0f);
}

// clock per-element typography (mirrors the Player / Target text system) : face / size / outline / colour / caps.
static Font* mm_font(FontManager* fm, Font* deflt, int e) {
    if (!fm) return deflt;
    const TextStyle& t = ui_config().mmText[e];
    const char* face = (t.face > 0) ? ui_font_face(t.face) : 0;
    Font* r = fm->get(face, t.bold ? 700 : 400, t.italic);
    return r ? r : deflt;
}
static inline float mm_sz(int e, float base) { return te_sz(ui_config().mmText[e], base); }
static inline float mm_ow(int e, float base) { return te_ow(ui_config().mmText[e], base); }
static inline u32   mm_col(int e, u32 base)  { return te_col(ui_config().mmText[e], base); }
static const char*  mm_up(int e, const char* s, char* buf, int cap) { return te_upper(ui_config().mmText[e], s, buf, cap); }   // UPPERCASE into buf if the element wants CAPS

// header height for the enabled clock rows (so the box + map shift by exactly what's shown).
static float clock_header_height(const UiConfig& c, float S) {
    if (!c.mmClock) return 0.0f;
    float h = snap(6.0f * S);
    if (c.mmClkTime) h += snap(24.0f * S);
    if (c.mmClkDay)  h += snap(22.0f * S);
    if (c.mmClkMoon) h += snap(30.0f * S);   // moon block : 2-column (graphic | % phase / next), ~30px band
    if (c.mmClkReal) h += snap(18.0f * S);   // separator + real/GMT (tighter : text sits at +13, little trailing)
    return h + snap(2.0f * S);
}
// natural content width of the clock header = the widest enabled row (mirrors draw_clock_header's measurements).
// LEFT/RIGHT placement hugs this instead of hdrW, so the header<->map gap matches the TOP layout's margin (bm).
static float clock_header_width(const Frame& f, const VanaClock& v, const UiConfig& c, u32 elemTex, float S) {
    if (!c.mmClock) return 0.0f;
    float w = 0.0f; char b0[48], b1[40], b2[40];
    const int di = (v.dayIdx < 0 || v.dayIdx > 7) ? 2 : v.dayIdx, ni = (di + 1) & 7;
    if (c.mmClkTime) {                                                      // Vana'diel time
        Font* tf = mm_font(f.fonts, f.font, MM_TIME);
        if (tf) { char tb[8]; sprintf(tb, "%d:%02d", v.hh, v.mm);
                  float rw = tf->measure(mm_up(MM_TIME, tb, b0, 48), snap(mm_sz(MM_TIME, 21.0f * S))); if (rw > w) w = rw; }
    }
    if (c.mmClkDay) {                                                       // [icon] Day > Next
        Font* df = mm_font(f.fonts, f.font, MM_DAY);
        if (df) { const float dsz = snap(mm_sz(MM_DAY, 11.0f * S)), nsz = snap(mm_sz(MM_DAY, 9.0f * S)), isz = snap(mm_sz(MM_DAY, 19.0f * S)), gp = snap(4.0f * S);
                  const char* const* DN = (c.lang == 1) ? VANA_DAYNAME_FR : VANA_DAYNAME;
                  const char* ds = mm_up(MM_DAY, DN[di], b0, 40); const char* ns = mm_up(MM_DAY, DN[ni], b1, 40);
                  float rw = (elemTex ? isz + gp : 0.0f) + df->measure(ds, dsz) + gp + df->measure(">", nsz) + gp + df->measure(ns, nsz); if (rw > w) w = rw; }
    }
    if (c.mmClkMoon) {                                                      // moon graphic | 2 data lines
        Font* mf = mm_font(f.fonts, f.font, MM_MOON);
        if (mf) { const float mr = snap(13.0f * S), gapMT = snap(7.0f * S), gp = snap(3.0f * S), dotD = snap(3.5f * S) * 2.0f;
                  const float psz = snap(mm_sz(MM_MOON, 10.0f * S)), nsz = snap(mm_sz(MM_MOON, 9.0f * S));
                  char pb[28], nb[16], fb[16], s1[24], s2[24];
                  const bool fr = c.lang == 1;
                  sprintf(pb, "%d%% %s", v.moonPct, fr ? moon_phase_name_fr(v.moonPhaseId) : moon_phase_name(v.moonPhaseId));
                  if (v.moonPct <= 2)  sprintf(nb, fr ? "maint." : "now"); else sprintf(nb, fr ? "%dj" : "%dd", v.daysToNew);
                  if (v.moonPct >= 98) sprintf(fb, fr ? "maint." : "now"); else sprintf(fb, fr ? "%dj" : "%dd", v.daysToFull);
                  sprintf(s1, fr ? "Nouv. %s" : "New %s", nb); sprintf(s2, fr ? "Pleine %s" : "Full %s", fb);
                  const char* tp = mm_up(MM_MOON, pb, b0, 48), *t1 = mm_up(MM_MOON, s1, b1, 40), *t2 = mm_up(MM_MOON, s2, b2, 40), *sep = "\xC2\xB7";
                  const float wp = mf->measure(tp, psz);
                  const float wnext = dotD + gp + mf->measure(t1, nsz) + gp + mf->measure(sep, nsz) + gp + dotD + gp + mf->measure(t2, nsz);
                  float rw = 2.0f * mr + gapMT + (wp > wnext ? wp : wnext); if (rw > w) w = rw; }
    }
    if (c.mmClkReal) {                                                      // Réel .. · GMT ..
        Font* rf = mm_font(f.fonts, f.font, MM_REAL);
        if (rf) { time_t t = time(0); struct tm lt, gt; localtime_s(&lt, &t); gmtime_s(&gt, &t);
                  char rb[48]; sprintf(rb, (c.lang == 1) ? "R\xC3\xA9""el %02d:%02d \xC2\xB7 GMT %02d:%02d" : "Real %02d:%02d \xC2\xB7 GMT %02d:%02d", lt.tm_hour, lt.tm_min, gt.tm_hour, gt.tm_min);
                  float rw = rf->measure(mm_up(MM_REAL, rb, b0, 48), snap(mm_sz(MM_REAL, 10.5f * S))); if (rw > w) w = rw; }
    }
    return w;
}
static void draw_clock_header(u32 dev, const Frame& f, const VanaClock& v, const UiConfig& c, u32 elemTex, u32 moonTex, float x, float y, float w, float S) {
    const float cx = x + w * 0.5f;
    const int di = (v.dayIdx < 0 || v.dayIdx > 7) ? 2 : v.dayIdx, ni = (di + 1) & 7;
    char up[40], up2[40];
    float cy = y + snap(6.0f * S);                                          // running top cursor
    if (c.mmClkTime) {                                                      // Vana'diel time (MM_TIME)
        Font* tf = mm_font(f.fonts, f.font, MM_TIME);
        if (tf) { tf->begin(dev);
            char tb[8]; sprintf(tb, "%d:%02d", v.hh, v.mm);
            tf->draw_c(dev, snap(cx), snap(cy + 11.0f * S), mm_up(MM_TIME, tb, up, 40), snap(mm_sz(MM_TIME, 21.0f * S)), mm_col(MM_TIME, 0xFFEAF0FFu), 0xFF000000u, mm_ow(MM_TIME, 1.7f * S)); }
        cy += snap(24.0f * S);
    }
    if (c.mmClkDay) {                                                       // [icon] Day > Next (MM_DAY)
        Font* df = mm_font(f.fonts, f.font, MM_DAY);
        if (df) { df->begin(dev);
            const float dsz = snap(mm_sz(MM_DAY, 11.0f * S)), nsz = snap(mm_sz(MM_DAY, 9.0f * S)), isz = snap(mm_sz(MM_DAY, 19.0f * S)), gp = snap(4.0f * S);
            const char* const* DN = (c.lang == 1) ? VANA_DAYNAME_FR : VANA_DAYNAME;
            const char* ds = mm_up(MM_DAY, DN[di], up, 40); const char* ns = mm_up(MM_DAY, DN[ni], up2, 40);
            const float dw = df->measure(ds, dsz), aw = df->measure(">", nsz), nw = df->measure(ns, nsz);
            const float rowW = (elemTex ? isz + gp : 0.0f) + dw + gp + aw + gp + nw;
            float rx = cx - rowW * 0.5f; const float rcy = cy + 11.0f * S;
            if (elemTex) {
                tex_state(dev, elemTex);
                tquad(dev, snap(rx), snap(rcy - isz * 0.5f), isz, isz, di / 8.0f, (di + 1) / 8.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
                dSetTex(dev, 0, 0); rx += isz + gp; df->begin(dev);
            }
            df->draw_lc(dev, snap(rx), snap(rcy), ds, dsz, mm_col(MM_DAY, VANA_DAYCOL[di]), 0xFF000000u, mm_ow(MM_DAY, 1.3f * S)); rx += dw + gp;
            df->draw_lc(dev, snap(rx), snap(rcy), ">", nsz, 0xFF8894A4u, 0xFF000000u, 1.0f * S); rx += aw + gp;
            df->draw_lc(dev, snap(rx), snap(rcy), ns, nsz, mm_col(MM_DAY, VANA_DAYCOL[ni]), 0xFF000000u, mm_ow(MM_DAY, 1.1f * S));
        }
        cy += snap(22.0f * S);
    }
    if (c.mmClkMoon) {                                                      // 2-COLUMN moon block : graphic | data
        Font* mf = mm_font(f.fonts, f.font, MM_MOON);                       // (saves ~22px vs the old stacked layout)
        const float mr = snap(13.0f * S), band = snap(30.0f * S), by = snap(cy + band * 0.5f);
        char pb[28], ns[16], fs[16], s1[24], s2[24], u0[28], u1[24], u2[24];
        const bool fr = c.lang == 1;
        sprintf(pb, "%d%% %s", v.moonPct, fr ? moon_phase_name_fr(v.moonPhaseId) : moon_phase_name(v.moonPhaseId));
        if (v.moonPct <= 2)  sprintf(ns, fr ? "maint." : "now"); else sprintf(ns, fr ? "%dj" : "%dd", v.daysToNew);
        if (v.moonPct >= 98) sprintf(fs, fr ? "maint." : "now"); else sprintf(fs, fr ? "%dj" : "%dd", v.daysToFull);
        sprintf(s1, fr ? "Nouv. %s" : "New %s", ns); sprintf(s2, fr ? "Pleine %s" : "Full %s", fs);
        const char* tp = mm_up(MM_MOON, pb, u0, 28), *t1 = mm_up(MM_MOON, s1, u1, 24), *t2 = mm_up(MM_MOON, s2, u2, 24), *sep = "\xC2\xB7";
        const float psz = snap(mm_sz(MM_MOON, 10.0f * S)), nsz = snap(mm_sz(MM_MOON, 9.0f * S));
        const float dotR = snap(3.5f * S), dotD = dotR * 2.0f, gp = snap(3.0f * S), gapMT = snap(7.0f * S);
        // measure both data lines (const width calc, no atlas needed) to centre the moon+data GROUP.
        float wp = 0.0f, w1 = 0.0f, w2 = 0.0f, wsep = 0.0f, wnext = 0.0f;
        if (mf) { wp = mf->measure(tp, psz); w1 = mf->measure(t1, nsz); w2 = mf->measure(t2, nsz); wsep = mf->measure(sep, nsz);
                  wnext = dotD + gp + w1 + gp + wsep + gp + dotD + gp + w2; }
        const float textW = wp > wnext ? wp : wnext;
        const float gx = snap(cx - (2.0f * mr + gapMT + textW) * 0.5f);    // group left edge
        draw_moon(dev, snap(gx + mr), by, mr, moonTex);                    // LEFT column : the moon graphic
        const float tx = gx + 2.0f * mr + gapMT;                          // RIGHT column : the two data lines
        if (mf) { mf->begin(dev);
            mf->draw_lv(dev, snap(tx), snap(by - 7.0f * S), tp, psz, mm_col(MM_MOON, 0xFFC6CEDCu), 0xFF000000u, mm_ow(MM_MOON, 1.2f * S));
            const float ny = snap(by + 7.0f * S), ow = mm_ow(MM_MOON, 1.1f * S);
            const u32 ncol = mm_col(MM_MOON, 0xFF9AA6BEu), dcol = mm_col(MM_MOON, 0xFF6E7890u);
            // order the two "next phase" chips by whichever is SOONER (Full first if it's closer than New).
            struct MoonChip { const char* t; float w; u32 rim, dsc; };
            const MoonChip cNew  = { t1, w1, 0x66BECDFFu, 0xFF0E1424u };   // New  : faint rim + dark disc
            const MoonChip cFull = { t2, w2, 0x66FFE9A8u, 0xFFFFE9A8u };   // Full : warm halo + bright disc
            const bool fullFirst = (v.daysToFull < v.daysToNew);
            const MoonChip& mA = fullFirst ? cFull : cNew;
            const MoonChip& mB = fullFirst ? cNew : cFull;
            float xc = tx;
            const float cxA = xc + dotR; xc += dotD + gp;                  // sooner phase, first
            mf->draw_lv(dev, snap(xc), ny, mA.t, nsz, ncol, 0xFF000000u, ow); xc += mA.w + gp;
            mf->draw_lv(dev, snap(xc), ny, sep, nsz, dcol, 0xFF000000u, ow); xc += wsep + gp;
            const float cxB = xc + dotR; xc += dotD + gp;                  // later phase, second
            mf->draw_lv(dev, snap(xc), ny, mB.t, nsz, ncol, 0xFF000000u, ow);
            color_state(dev);                                             // dots = untextured discs
            disc(dev, snap(cxA), ny, dotR + snap(1.0f), mA.rim); disc(dev, snap(cxA), ny, dotR, mA.dsc);
            disc(dev, snap(cxB), ny, dotR + snap(1.0f), mB.rim); disc(dev, snap(cxB), ny, dotR, mB.dsc);
        }
        cy += band;
    }
    if (c.mmClkReal) {                                                      // separator + real / GMT (MM_REAL)
        color_state(dev);
        grad_quad(dev, snap(x + 14.0f * S), snap(cy + 3.0f * S), w - snap(28.0f * S), snap(1.0f), 0x33A0C0FFu, 0x33A0C0FFu, 0x33A0C0FFu, 0x33A0C0FFu);
        Font* rf = mm_font(f.fonts, f.font, MM_REAL);
        if (rf) { rf->begin(dev);
            time_t t = time(0); struct tm lt, gt; localtime_s(&lt, &t); gmtime_s(&gt, &t);
            char rb[48]; sprintf(rb, (c.lang == 1) ? "R\xC3\xA9""el %02d:%02d \xC2\xB7 GMT %02d:%02d" : "Real %02d:%02d \xC2\xB7 GMT %02d:%02d", lt.tm_hour, lt.tm_min, gt.tm_hour, gt.tm_min);
            rf->draw_c(dev, snap(cx), snap(cy + 13.0f * S), mm_up(MM_REAL, rb, up, 40), snap(mm_sz(MM_REAL, 10.5f * S)), mm_col(MM_REAL, 0xFFC8D2E6u), 0xFF000000u, mm_ow(MM_REAL, 1.2f * S));
        }
        cy += snap(24.0f * S);
    }
    (void)cy;
}

// ================= COMPACT layout (the "Sentinel" look) : the clock and the map info are two THIN BANDS, each on
// one line, above or below the map (mmClkBand / mmInfoBand), the box no wider than the map. Every piece of a band
// sits CENTRED IN ITS OWN CELL (the slack is shared equally), cells split by full-height rules ; the frame is its
// own : a dark bevelled plate (light edge on top, soft drop shadow) with a recessed well around the map. =========
static float clock_band_h(const UiConfig& c, float S) {
    if (!c.mmClock || !(c.mmClkTime || c.mmClkDay || c.mmClkMoon || c.mmClkReal)) return 0.0f;
    // just the text plus a small breath above and below : the cap height of the tallest piece (the time) + 2 x 5 px
    const float tsz = mm_sz(MM_TIME, 25.0f * S);
    float h = (c.mmClkTime || c.mmClkDay || c.mmClkMoon) ? snap(tsz * 0.72f + 10.0f * S) : snap(4.0f * S);
    if (c.mmClkReal && c.mmInfoBand <= 0) h += snap(14.0f * S);             // the real / GMT line under it -- only when there is no info band to carry it
    return h;
}
static float info_band_h(const UiConfig& c, float S) { return (c.mmInfoBand > 0) ? snap(18.0f * S * 0.72f + 10.0f * S) : 0.0f; }   // the square's cap height + 2 x 5 px

// the band face : serif bold (the concept's numerals) unless the user picked a face for that element
static Font* band_font(const Frame& f, int e) {
    const TextStyle& t = ui_config().mmText[e];
    if (t.face > 0 || !f.fonts) return mm_font(f.fonts, f.font, e);
    Font* r = f.fonts->get("Cambria", 700, t.italic);                   // LINING figures (Georgia's old-style 3 4 5 7 9 dropped below the line on the wheels) ; an engraved serif, on every Windows (the user 2026-11-25)
    return r ? r : f.font;
}

static void band_rule(u32 dev, float x, float y0, float y1) {               // a full-height cell rule : dark line + light line
    color_state(dev);
    const float h = y1 - y0;
    grad_quad(dev, snap(x),        snap(y0), 1.0f, h, 0x00000000u, 0x00000000u, 0xAA000000u, 0xAA000000u);
    grad_quad(dev, snap(x) + 1.0f, snap(y0), 1.0f, h, 0x00FFFFFFu, 0x00FFFFFFu, 0x30FFFFFFu, 0x30FFFFFFu);
    grad_quad(dev, snap(x),        snap(y0 + h * 0.5f), 1.0f, h * 0.5f, 0xAA000000u, 0xAA000000u, 0x00000000u, 0x00000000u);
    grad_quad(dev, snap(x) + 1.0f, snap(y0 + h * 0.5f), 1.0f, h * 0.5f, 0x30FFFFFFu, 0x30FFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu);
}

// n cells of content widths cw[] across [x, x+w] : the slack is shared equally, each content centred in its cell.
// Returns each content's LEFT x in cx[] and the rule positions in rx[] (n-1).
static void band_cells(const float* cw, int n, float x, float w, float* cx, float* rx) {
    float sum = 0.0f; for (int i = 0; i < n; ++i) sum += cw[i];
    float slack = (w - sum) / (float)(n > 0 ? n : 1); if (slack < 0.0f) slack = 0.0f;
    float cur = x + ((w - sum - slack * n) > 0.0f ? (w - sum - slack * n) * 0.5f : 0.0f);
    for (int i = 0; i < n; ++i) {
        cx[i] = cur + slack * 0.5f;
        cur += cw[i] + slack;
        if (i < n - 1) rx[i] = cur;
    }
}

// one line : TIME | [icon] Day > [icon] Next | (moon) 98 %  ; the next day's NAME and then today's name drop out
// (icons stay) when the map is too narrow for them -- nothing enabled is ever lost, only shortened.
static void draw_clock_band(u32 dev, const Frame& f, const VanaClock& v, const UiConfig& c, u32 elemTex, u32 moonTex, float x, float y, float w, float S) {
    const int di = (v.dayIdx < 0 || v.dayIdx > 7) ? 2 : v.dayIdx, ni = (di + 1) & 7;
    const bool line1 = c.mmClkTime || c.mmClkDay || c.mmClkMoon;
    const float bandH = snap(mm_sz(MM_TIME, 25.0f * S) * 0.72f + 10.0f * S), cy = y + bandH * 0.5f, sg = snap(4.0f * S), pad = snap(6.0f * S);
    char tb[8], u0[40], u1[40], u2[40], mb[8];
    Font* tf = band_font(f, MM_TIME);
    Font* df = band_font(f, MM_DAY);
    Font* mf = mm_font(f.fonts, f.font, MM_MOON);
    const float tsz = snap(mm_sz(MM_TIME, 25.0f * S)), dsz = snap(mm_sz(MM_DAY, 15.0f * S)), nsz = snap(mm_sz(MM_DAY, 11.0f * S));
    const float isz = snap(mm_sz(MM_DAY, 18.0f * S)), nisz = snap(mm_sz(MM_DAY, 13.0f * S)), msz = snap(mm_sz(MM_MOON, 14.0f * S)), mr = snap(9.0f * S);
    sprintf(tb, "%02d:%02d", v.hh, v.mm); sprintf(mb, "%d%%", v.moonPct);
    const char* const* DN = (c.lang == 1) ? VANA_DAYNAME_FR : VANA_DAYNAME;
    const char* ts = mm_up(MM_TIME, tb, u0, 40), *ds = mm_up(MM_DAY, DN[di], u1, 40), *ns = mm_up(MM_DAY, DN[ni], u2, 40);
    const float wt = (c.mmClkTime && tf) ? tf->measure(ts, tsz) : 0.0f;
    const float wm = (c.mmClkMoon && mf) ? 2.0f * mr + sg + mf->measure(mb, msz) : 0.0f;
    const float wd = df ? df->measure(ds, dsz) : 0.0f, wa = df ? df->measure(">", nsz) : 0.0f, wn = df ? df->measure(ns, nsz) : 0.0f;
    const float ic = elemTex ? isz + sg : 0.0f, nic = elemTex ? nisz : 0.0f;
    const int nseg = (c.mmClkTime ? 1 : 0) + (c.mmClkDay ? 1 : 0) + (c.mmClkMoon ? 1 : 0);
    const float minGap = snap(10.0f * S);                                  // the least air a cell keeps around its content
    const float avail = w - 2.0f * pad - nseg * minGap - wt - wm;
    // the day segment, widest form first : icon Day > icon Next  ->  icon Day > icon  ->  icon > icon
    int dayForm = 0; float wday = 0.0f;
    if (c.mmClkDay) {
        const float f0 = ic + wd + sg + wa + sg + nic + (elemTex ? sg : 0.0f) + wn;
        const float f1 = ic + wd + sg + wa + sg + (elemTex ? nic : wn);
        const float f2 = ic + wa + sg + (elemTex ? nic : wn);
        if (f0 <= avail || !elemTex) { dayForm = (f0 <= avail || !elemTex) ? 0 : 1; wday = f0; }
        else if (f1 <= avail) { dayForm = 1; wday = f1; }
        else { dayForm = 2; wday = f2; }
    }
    if (line1 && nseg > 0) {
        float cw[3], cx[3], rx[2]; int n = 0;
        if (c.mmClkTime) cw[n++] = wt;
        if (c.mmClkDay)  cw[n++] = wday;
        if (c.mmClkMoon) cw[n++] = wm;
        band_cells(cw, n, x + pad, w - 2.0f * pad, cx, rx);
        for (int i = 0; i < n - 1; ++i) band_rule(dev, rx[i], y + snap(4.0f * S), y + bandH - snap(4.0f * S));
        int k = 0;
        if (c.mmClkTime && tf) {
            tf->begin(dev);
            tf->draw_lc(dev, snap(cx[k]), snap(cy), ts, tsz, mm_col(MM_TIME, 0xFFF4EEDFu), 0xFF000000u, mm_ow(MM_TIME, 1.4f * S));
            ++k;
        }
        if (c.mmClkDay && df) {
            float rx0 = cx[k++];
            if (elemTex) { tex_state(dev, elemTex);
                tquad(dev, snap(rx0), snap(cy - isz * 0.5f), isz, isz, di / 8.0f, (di + 1) / 8.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
                dSetTex(dev, 0, 0); rx0 += isz + sg; }
            df->begin(dev);
            if (dayForm < 2) { df->draw_lc(dev, snap(rx0), snap(cy), ds, dsz, mm_col(MM_DAY, VANA_DAYCOL[di]), 0xFF000000u, mm_ow(MM_DAY, 1.2f * S)); rx0 += wd + sg; }
            df->draw_lc(dev, snap(rx0), snap(cy), ">", nsz, 0xFF8894A4u, 0xFF000000u, 1.0f * S); rx0 += wa + sg;
            if (elemTex) { tex_state(dev, elemTex);
                tquad(dev, snap(rx0), snap(cy - nisz * 0.5f), nisz, nisz, ni / 8.0f, (ni + 1) / 8.0f, 0.0f, 1.0f, 0xC8FFFFFFu, 0xC8FFFFFFu);
                dSetTex(dev, 0, 0); rx0 += nisz + sg; df->begin(dev); }
            if (dayForm == 0) df->draw_lc(dev, snap(rx0), snap(cy), ns, nsz, mm_col(MM_DAY, VANA_DAYCOL[ni]), 0xFF000000u, mm_ow(MM_DAY, 1.1f * S));
        }
        if (c.mmClkMoon && mf) {
            const float rx0 = cx[k++];
            draw_moon(dev, snap(rx0 + mr), snap(cy), mr, moonTex);
            mf->begin(dev);
            mf->draw_lc(dev, snap(rx0 + 2.0f * mr + sg), snap(cy), mb, msz, mm_col(MM_MOON, 0xFFE6E9F0u), 0xFF000000u, mm_ow(MM_MOON, 1.2f * S));
        }
    }
    if (c.mmClkReal && c.mmInfoBand <= 0) {                                 // Real .. · GMT .. , small, under the line (the info band carries it when shown)
        Font* rf = mm_font(f.fonts, f.font, MM_REAL);
        if (rf) { rf->begin(dev);
            time_t t = time(0); struct tm lt, gt; localtime_s(&lt, &t); gmtime_s(&gt, &t);
            char rb[48]; sprintf(rb, (c.lang == 1) ? "R\xC3\xA9""el %02d:%02d \xC2\xB7 GMT %02d:%02d" : "Real %02d:%02d \xC2\xB7 GMT %02d:%02d", lt.tm_hour, lt.tm_min, gt.tm_hour, gt.tm_min);
            const float ry = line1 ? y + bandH + snap(5.0f * S) : y + snap(9.0f * S);
            rf->draw_c(dev, snap(x + w * 0.5f), snap(ry), mm_up(MM_REAL, rb, u0, 40), snap(mm_sz(MM_REAL, 10.0f * S)), mm_col(MM_REAL, 0xFFC8D2E6u), 0xFF000000u, mm_ow(MM_REAL, 1.1f * S));
        }
    }
}

// one line : F-10 | 2.0x | (reticle) 12.4y | (scan) 48y | 17:43 · 16:43 GMT -- each piece in its own cell, only
// when it has something to say
static void fmt_yalms(char* b, float d) { if (d < 100.0f) sprintf(b, "%.1fy", d); else sprintf(b, "%dy", (int)(d + 0.5f)); }
static void draw_info_band(u32 dev, const Frame& f, const UiConfig& c, const GameState& g, float mapX, float mapY, float x, float y, float w, float S) {
    Font* sf = band_font(f, MM_TIME);                                        // the square / zoom : the serif numerals
    Font* fo = f.font; if (!fo || !sf) return;
    const float bandH = info_band_h(c, S), cy = y + bandH * 0.5f, sg = snap(4.0f * S), ir = snap(5.5f * S), pad = snap(6.0f * S);
    const float gsz = snap(18.0f * S), zsz = snap(14.0f * S), vsz = snap(13.0f * S);
    char gb[12] = "", zb[12], tdb[16] = "", sdb[16] = "", rb[32] = "";
    if (c.mmGrid) {
        const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
        if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) sprintf(gb, "%c-%d", 'A' + gc, gr + 1);
    }
    sprintf(zb, "%.1fx", clampf(c.mmZoom, 1.0f, 24.0f));
    if (c.mmInfoDist && g.target.valid) { const float dx = g.target.posX - g.meX, dz = g.target.posZ - g.meZ; fmt_yalms(tdb, sqrtf(dx * dx + dz * dz)); }
    if (c.mmInfoDist && c.mmScan) {
        const PartyState::ScanTrack& st = party().scan_track();
        if (st.ms && (unsigned)(model_now_ms() - st.ms) < 3000u) { const float dx = st.x - g.meX, dz = st.z - g.meZ; fmt_yalms(sdb, sqrtf(dx * dx + dz * dz)); }
    }
    // cells : 0 square, 1 zoom, 2 target, 3 scan, 4 real/GMT
    float cw[5]; int id[5]; int n = 0;
    if (gb[0])  { id[n] = 0; cw[n++] = sf->measure(gb, gsz); }
    id[n] = 1; cw[n++] = sf->measure(zb, zsz);
    if (tdb[0]) { id[n] = 2; cw[n++] = 2.0f * ir + sg + fo->measure(tdb, vsz); }
    if (sdb[0]) { id[n] = 3; cw[n++] = 2.0f * ir + sg + fo->measure(sdb, vsz); }
    Font* rf = mm_font(f.fonts, f.font, MM_REAL);
    const float rsz = snap(mm_sz(MM_REAL, 12.0f * S));
    if (c.mmClock && c.mmClkReal && rf) {                                    // "17:43 · 16:43 GMT" ; only "17:43" if too narrow
        time_t t = time(0); struct tm lt, gt; localtime_s(&lt, &t); gmtime_s(&gt, &t);
        float used = 0.0f; for (int i = 0; i < n; ++i) used += cw[i];
        const float room = w - 2.0f * pad - used - (n + 1) * snap(10.0f * S);
        sprintf(rb, "%02d:%02d \xC2\xB7 %02d:%02d GMT", lt.tm_hour, lt.tm_min, gt.tm_hour, gt.tm_min);
        if (rf->measure(rb, rsz) > room) sprintf(rb, "%02d:%02d", lt.tm_hour, lt.tm_min);
        id[n] = 4; cw[n++] = rf->measure(rb, rsz);
    }
    float cx[5], rx[4];
    band_cells(cw, n, x + pad, w - 2.0f * pad, cx, rx);
    for (int i = 0; i < n - 1; ++i) band_rule(dev, rx[i], y + snap(4.0f * S), y + bandH - snap(4.0f * S));
    for (int i = 0; i < n; ++i) {
        const float lx = cx[i];
        switch (id[i]) {
        case 0: sf->begin(dev); sf->draw_lc(dev, snap(lx), snap(cy), gb, gsz, 0xFFF4EEDFu, 0xFF000000u, 1.3f * S); break;
        case 1: sf->begin(dev); sf->draw_lc(dev, snap(lx), snap(cy), zb, zsz, 0xFFC9CED6u, 0xFF000000u, 1.1f * S); break;
        case 2: {                                                         // your target : a reticle, its claim colour
            u32 lc = c.mmTgtLineCol | 0xFF000000u;
            for (int e = 0; e < g.mapEntN; ++e) if (g.mapEnts[e].id == g.target.id) { lc = mm_ent_color(g.mapEnts[e]) | 0xFF000000u; break; }
            color_state(dev);
            const float icx = snap(lx + ir), icy = snap(cy);
            rrect_stroke(dev, icx - ir, icy - ir, 2.0f * ir, 2.0f * ir, ir, lc, snap(1.5f * S));
            disc(dev, icx, icy, snap(1.4f * S), lc);
            fo->begin(dev); fo->draw_lc(dev, snap(lx + 2.0f * ir + sg), snap(cy), tdb, vsz, 0xFFF2F5FAu, 0xFF000000u, 1.1f * S);
        } break;
        case 3: {                                                         // the Wide Scan track : its ring, its colour
            const u32 sc = c.mmScanCol | 0xFF000000u;
            color_state(dev);
            const float icx = snap(lx + ir), icy = snap(cy), ph = f.t * 2.2f - floorf(f.t * 2.2f), rp = ir * (0.6f + 0.6f * ph);
            rrect_stroke(dev, icx - rp, icy - rp, 2.0f * rp, 2.0f * rp, rp, (sc & 0x00FFFFFFu) | ((u32)((1.0f - ph) * 200.0f) << 24), snap(1.2f * S));
            rrect_stroke(dev, icx - ir * 0.6f, icy - ir * 0.6f, 1.2f * ir, 1.2f * ir, ir * 0.6f, sc, snap(1.5f * S));
            fo->begin(dev); fo->draw_lc(dev, snap(lx + 2.0f * ir + sg), snap(cy), sdb, vsz, sc, 0xFF000000u, 1.1f * S);
        } break;
        case 4: { char ru[32]; rf->begin(dev);
            rf->draw_lc(dev, snap(lx), snap(cy), mm_up(MM_REAL, rb, ru, 32), rsz, mm_col(MM_REAL, 0xFFC8D2E6u), 0xFF000000u, mm_ow(MM_REAL, 1.1f * S)); } break;
        }
    }
}

// the compact plate : soft drop shadow, a metal edge lighter on top, a dark body, each band its own gradient with a
// light bevel line on its top edge, and a recessed WELL around the map (dark rim + a faint light line outside it)
static void draw_compact_plate(u32 dev, float x, float y, float w, float h, float mx, float my, float md, bool round,
                               float cbY, float cbH, float ibY, float ibH, float S) {
    (void)x; (void)y; (void)h;   // the BOX itself is the user's themed box (draw_themed_box, mmBox) : this only adds the bands and the well
    color_state(dev);
    struct B { float y, h; } bands[2] = { { cbY, cbH }, { ibY, ibH } };
    for (int i = 0; i < 2; ++i) {
        if (bands[i].h <= 0.0f) continue;
        const float by = snap(bands[i].y), bh = snap(bands[i].h);
        const float bx = x + snap(2.0f * S), bw = w - snap(4.0f * S);
        rrect(dev, bx, by + 1.0f, bw, bh - 2.0f, snap(3.0f * S), 0x1EFFFFFFu, 0x06FFFFFFu);   // the band's own face : a LIGHT lift over the theme (relief = lighter)
        grad_quad(dev, bx + snap(3.0f * S), by + 1.0f, bw * 0.5f - snap(3.0f * S), 1.0f, 0x00FFFFFFu, 0x40FFFFFFu, 0x00FFFFFFu, 0x40FFFFFFu);   // top bevel, fading at the ends
        grad_quad(dev, bx + bw * 0.5f, by + 1.0f, bw * 0.5f - snap(3.0f * S), 1.0f, 0x40FFFFFFu, 0x00FFFFFFu, 0x40FFFFFFu, 0x00FFFFFFu);
    }
    // the map well
    if (round) {
        const float cxm = mx + md * 0.5f, cym = my + md * 0.5f, rr = md * 0.5f;
        rrect_stroke(dev, cxm - rr - 2.0f, cym - rr - 2.0f, md + 4.0f, md + 4.0f, rr + 2.0f, 0x38FFFFFFu, 1.0f);
        rrect_stroke(dev, cxm - rr - 1.0f, cym - rr - 1.0f, md + 2.0f, md + 2.0f, rr + 1.0f, 0xFF06080Au, 1.6f);
    } else {
        rrect_stroke(dev, mx - 2.0f, my - 2.0f, md + 4.0f, md + 4.0f, snap(2.0f * S), 0x38FFFFFFu, 1.0f);
        rrect_stroke(dev, mx - 1.0f, my - 1.0f, md + 2.0f, md + 2.0f, snap(1.0f * S), 0xFF06080Au, 1.6f);
    }
}

// ---- the DIAL INSTRUMENT (layout 2, round + Astral) -- SHOW, DON'T TELL (the user's rule) : every clock fact is a
// shape or a colour on the dial ; the only numbers are two small ones, each where the eye already is.
//   * the DAY RING, thin and quiet, outside the brass : 24 Vana'diel hours, noon at the top, midnight at the bottom ;
//   * the SUN is the day's ELEMENT ICON, haloed in its colour, travelling on it ; the exact time a small label just
//     outside the ring, next to it ;
//   * TOMORROW's element icon sits at midnight (the bottom) : "tomorrow begins here" ;
//   * the MOON is the marker at night ; by day it rides opposite the sun, smaller -- its phase IS the moon picture ;
//   * the map square a small pill at the lens's foot ; the distances on the target / the Wide Scan mark ; the zoom
//     only while it changes ; the cardinals engraved in the brass, N in red.
// Sizes are TUNE knobs ("mm.dial_*") ; "mm.test_hour / test_day / test_moon" force the clock (dev tests).
static VanaClock dial_vana(const VanaClock& in) {               // the dev test overrides (TUNE -> -1 = the real clock)
    VanaClock v = in;
    const float th = TUNE("mm.test_hour", -1.0f), td = TUNE("mm.test_day", -1.0f), tm = TUNE("mm.test_moon", -1.0f);
    if (th >= 0.0f) { v.hh = (int)th % 24; v.mm = (int)((th - (float)(int)th) * 60.0f); }
    if (td >= 0.0f) v.dayIdx = (int)td & 7;
    if (tm >= 0.0f) { v.moonPct = (int)tm; if (v.moonPct > 100) v.moonPct = 100; v.daysToFull = (100 - v.moonPct) / 3; v.daysToNew = v.moonPct / 3 + 42; }
    // "mm.test_speed" : a TIME-LAPSE for videos (Vana'diel hours per real second) -- the hour, the day of the week and the
    // moon's phase all run from it
    const float sp = TUNE("mm.test_speed", 0.0f);
    if (sp > 0.0f) {
        const double H = (double)model_now_ms() / 1000.0 * (double)sp;
        const double hh = fmod(H, 24.0), dd = H / 24.0;
        v.hh = (int)hh; v.mm = (int)((hh - (double)v.hh) * 60.0); v.dayIdx = (int)fmod(dd, 8.0);
        const double ph = fmod(dd * 4.0, 84.0) / 84.0;                   // the moon : 4 x faster, so a week shows its phases
        v.moonPct = (int)((1.0 - cos(ph * 6.2831853)) * 50.0); v.waning = ph > 0.5;
        const int dn = (int)((1.0 - ph) * 84.0) % 84, df = (int)((ph < 0.5 ? 0.5 - ph : 1.5 - ph) * 84.0) % 84;
        v.daysToNew = dn; v.daysToFull = df;
    }
    return v;
}
// ---- the dial's METAL follows the HUD theme : Minimap > Frame = Theme (Same as Party : the party box's FFXI skin or
// procedural theme, its hue and luminosity) / Custom (its colour) / None (the original brass). One colour B, a metal
// palette derived from it (normalised so a dark theme colour still reads as polished metal). The enamel faces (sky,
// moon, champagne) keep their natural colours. ----
struct DialMetal { u32 edge, body, top, bot, dark, lite, glint, shade; };
static DialMetal g_dm = { 0xFF3A2A10u, 0xFFB79A61u, 0xFFD4B574u, 0xFF8E7440u, 0xFF5E4A26u, 0xFFE2C482u, 0xFFFFF0C8u, 0xFF241808u };
#define MA(a, col) ((((u32)(a)) << 24) | ((col) & 0x00FFFFFFu))
static u32 dm_scale(u32 c, float k) {
    const float r = ((c >> 16) & 0xFF) * k, g = ((c >> 8) & 0xFF) * k, b = (c & 0xFF) * k;
    return 0xFF000000u | ((u32)clampf(r, 0.0f, 255.0f) << 16) | ((u32)clampf(g, 0.0f, 255.0f) << 8) | (u32)clampf(b, 0.0f, 255.0f);
}
static u32 dm_toward(u32 c, u32 t, float k) {
    const float q = 1.0f - k;
    return 0xFF000000u | ((u32)(((c >> 16) & 0xFF) * q + ((t >> 16) & 0xFF) * k) << 16) | ((u32)(((c >> 8) & 0xFF) * q + ((t >> 8) & 0xFF) * k) << 8)
                       | (u32)((c & 0xFF) * q + (t & 0xFF) * k);
}
static void dial_metal_set(u32 base) {
    if (!base) { g_dm = { 0xFF3A2A10u, 0xFFB79A61u, 0xFFD4B574u, 0xFF8E7440u, 0xFF5E4A26u, 0xFFE2C482u, 0xFFFFF0C8u, 0xFF241808u }; return; }   // the brass
    const u32 mx = ((base >> 16) & 0xFF) > ((base >> 8) & 0xFF) ? (((base >> 16) & 0xFF) > (base & 0xFF) ? (base >> 16) & 0xFF : base & 0xFF)
                                                              : (((base >> 8) & 0xFF) > (base & 0xFF) ? (base >> 8) & 0xFF : base & 0xFF);
    const u32 B = mx ? dm_scale(base, 190.0f / (float)mx) : 0xFF9A9A9Au;                          // a polished metal's brightness
    g_dm.body = B;
    g_dm.top = dm_toward(B, 0xFFFFFFFFu, 0.25f);
    g_dm.bot = dm_scale(B, 0.68f);
    g_dm.edge = dm_scale(B, 0.26f);
    g_dm.dark = dm_scale(B, 0.46f);
    g_dm.lite = dm_toward(B, 0xFFFFFFFFu, 0.42f);
    g_dm.glint = dm_toward(B, 0xFFFFFFFFu, 0.80f);
    g_dm.shade = dm_scale(B, 0.16f);
}

// ---- goldsmithing : a cast LUG between two pieces of the case. From A (half-width aw) to B (half-width bw), its two
// flanks are quadratic curves pulled in to `waist` at the middle -- flared at both ends like a watch's horns. The body
// is solid triangles (no internal seams), its outline an anti-aliased stroke of the same colour ; drawn twice, a dark
// edge then the brass, then a light filet on the upper flank. ----

// the four HORNS joining the corner subdials to the ring : drawn BEFORE the ring and the subdials, so both pieces sit
// on them and each horn comes out from under both -- one cast object, not a lug laid on top

// ---- the SQUARE watch case (Dial layout, square shape) : a bevelled brass frame around the map, lit from the top,
// a chamfer into the map, engraved hour marks along its sides (the cardinals are engraved at mid-sides by the
// instrument). Drawn BEFORE the map, which then fills the inside. ----
static void draw_square_case(u32 dev, float cx, float cy, float r, float fi, float S) {
    const float x0 = cx - r, y0 = cy - r, W = 2.0f * r, rad = snap(6.0f * S), ri = r - fi;
    color_state(dev);
    rrect_glow(dev, x0, y0 + 2.0f * S, W, W, rad, 0x90000000u, snap(6.0f * S));
    rrect(dev, x0 - 1.4f * S, y0 - 1.4f * S, W + 2.8f * S, W + 2.8f * S, rad + 1.4f * S, g_dm.edge, g_dm.edge);   // the dark edge
    rrect(dev, x0, y0, W, W, rad, g_dm.top, g_dm.bot);                                                          // the brass, lit from the top
    rrect_stroke(dev, x0 + 1.0f * S, y0 + 1.0f * S, W - 2.0f * S, W - 2.0f * S, rad - 1.0f * S, MA(0x60, g_dm.glint), 1.0f);   // a polished outer filet
    rrect_stroke(dev, cx - ri - 3.0f * S, cy - ri - 3.0f * S, 2.0f * ri + 6.0f * S, 2.0f * ri + 6.0f * S, snap(2.5f * S), MA(0x50, g_dm.shade), 1.0f);   // an engraved line
    rrect(dev, cx - ri - 1.8f * S, cy - ri - 1.8f * S, 2.0f * ri + 3.6f * S, 2.0f * ri + 3.6f * S, snap(2.2f * S), g_dm.dark, g_dm.lite);   // the chamfer : dark at the top, lit at the bottom
    rrect(dev, cx - ri, cy - ri, 2.0f * ri, 2.0f * ri, snap(1.5f * S), 0xFF140F08u, 0xFF140F08u);                    // the seat of the map
    const float mid = (r + ri) * 0.5f;                                                                                // the engraved marks : 6 per side
    for (int side = 0; side < 4; ++side)
        for (int k = 1; k <= 7; ++k) {
            if (k == 4) continue;                                                                                     // the cardinal's place
            const float tt = -1.0f + 2.0f * (float)k / 8.0f, len = (k == 2 || k == 6) ? fi * 0.42f : fi * 0.26f;
            float ax, ay, bx, by;
            if (side == 0)      { ax = cx + tt * mid; ay = cy - mid - len * 0.5f; bx = ax; by = ay + len; }
            else if (side == 1) { ay = cy + tt * mid; ax = cx + mid - len * 0.5f; by = ay; bx = ax + len; }
            else if (side == 2) { ax = cx + tt * mid; ay = cy + mid - len * 0.5f; bx = ax; by = ay + len; }
            else                { ay = cy + tt * mid; ax = cx - mid - len * 0.5f; by = ay; bx = ax + len; }
            seg_soft(dev, ax + 0.6f, ay + 0.7f, bx + 0.6f, by + 0.7f, 1.0f * S, MA(0x80, g_dm.glint));                          // the light in the cut
            seg_soft(dev, ax, ay, bx, by, 1.0f * S, g_dm.edge);                                                    // the cut
        }
    color_state(dev);
}

// THE DAY ORB (the user 2026-11-25 : "a ball with colour bands, in 3D, in a dial of water ; it turns horizontally on a
// north-south axis, the quarters running top to bottom" ; the 8 days ; vivid element colours ; then "better -- heroic
// fantasy technology, a little magical") : an ENCHANTED GLOBE of 8 slices, one per day in its element's colour, 45
// degrees each, pole to pole, spinning on its upright axis with the hours (today faces you at the start of the day,
// tomorrow at midnight). Gold filigree meridians between the slices ; every slice lit from WITHIN (today the brightest),
// a slow shimmer running through the colours, a rim of light of today's colour (the crystal's edge) ; outside, today's aura breathing, and a glass shell : a crescent reflection and a glint.
// Rendered as a polar grid over the visible disc : each vertex's normal gives its longitude (-> its slice) and its
// latitude, and is lit. Colour-quad state ; additive passes reset the blend. No allocation.
static const u32 ORB_ELEM[8] = { 0xFFE0261Au, 0xFFB07A22u, 0xFF1C4FD6u, 0xFF22A83Cu, 0xFF2EC4F0u, 0xFF9A2EE0u, 0xFFF4F1E8u, 0xFF2A1446u };
static const u32 ORB_GLOW[8] = { 0xFFFF6A3Cu, 0xFFE8B850u, 0xFF4C8CFFu, 0xFF5CE07Au, 0xFF8CE8FFu, 0xFFC878FFu, 0xFFFFF6D8u, 0xFF9A5CFFu };   // each element's LIGHT (Dark glows violet)
// ONE LAMP for the whole instrument (the user 2026-11-25 : "think each sphere's lighting from its position") : a light
// hanging above the dial's centre ; every sphere is lit on the side that faces it (the top-left subdial from the lower
// right, the day orb from the upper left, ...) and its glass reflection sits on that side. Set by draw_dial_instrument.
// (MESH CACHES : see sphere_mesh_c below -- the key every cached mesh compares)
struct MeshKey { float k[10]; };
static bool mesh_key_changed(MeshKey& held, const MeshKey& now, bool& valid) {
    if (valid && memcmp(held.k, now.k, sizeof(held.k)) == 0) return false;
    held = now; valid = true; return true;
}
static float s_lampX = 0.0f, s_lampY = 0.0f, s_lampH = 1.0f;
struct Lamp { float x, y, z; };
static Lamp lamp_for(float cx, float cy) {
    float dx = s_lampX - cx, dy = s_lampY - cy;
    const float d = sqrtf(dx * dx + dy * dy);
    if (d < 1e-3f) { dx = -0.55f; dy = -0.65f; } else { dx /= d; dy /= d; }   // at the lamp itself : the old upper left
    Lamp L = { dx * 0.62f, dy * 0.62f, 0.78f };                         // a raking light : 0.62 sideways, 0.78 toward you
    return L;
}
// CLOISONNE ENAMEL, A FABERGE GLOBE (the user 2026-11-25 : keep the coloured quarters, no icon, but "not a beach ball") :
// the toy look was bright flat colours on glossy plastic. Here each day is a JEWEL-toned translucent enamel (ruby, topaz,
// sapphire, emerald, aquamarine, amethyst, pearl, onyx) laid over an engine-turned GUILLOCHE (fine waves that show through),
// thicker and lighter at the slice's heart, darker where it pools against the GOLD CLOISONS ; today a little brighter, the
// others not switched off ; a faint gilt rim. Lit by the instrument's one lamp.
// the ELEMENTS' colours as the HD element icons paint them (measured on assets/weather_icons.raw, the user 2026-11-25 :
// "the elements' colours are not respected") -- spread apart where two were too close (Water deep / Ice light blue,
// Lightning bright violet / Dark deep violet) : Fire red, Earth copper, Water deep blue, Wind green, Ice light blue,
// Lightning violet, Light gold, Dark deep violet
static const u32 ORB_JEWEL[8] = { 0xFFC8180Au, 0xFFA8561Au, 0xFF0A3FC8u, 0xFF18A034u, 0xFF3C9BF0u, 0xFF9A3CF0u, 0xFFE8A82Au, 0xFF3A0C78u };
static u32 orb_col(float u, float v, float z, int di, float frac, float t, const Lamp& Lp) {
    (void)t;
    const float lon = atan2f(u, z), lat = asinf(v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v));
    const float band = 0.7853982f;                                      // 45 degrees a day
    const float pp = lon / band + frac - 0.5f;                          // in days from today : the needle (lon 0) ENTERS today's slice at
                                                                        // midnight, is at its heart at noon, leaves it at the next midnight
                                                                        // (the user 2026-11-25 : lit for the whole day, not from noon)
    const int d0 = (int)floorf(pp + 0.5f);
    const float edge = (pp + 0.5f) - floorf(pp + 0.5f);                // 0..1 across the slice
    auto day = [&](int d) { return (di + (d % 8) + 16) & 7; };
    const u32 base = ORB_JEWEL[day(d0)];
    float dif = Lp.x * u + Lp.y * v + Lp.z * z; if (dif < 0.0f) dif = 0.0f;
    const float k = (0.42f + 0.78f * dif) * (0.62f + 0.38f * z);
    const float heart = sinf(edge * 3.1415927f);                       // 0 at the cloisons, 1 at the slice's heart
    const float pool = 0.62f + 0.38f * powf(heart, 0.6f);              // the enamel pools darker against the gold
    // the GUILLOCHE under the enamel : fine waves following the latitude, rippled by the longitude
    const float gl = 0.5f + 0.5f * sinf(lat * 18.0f + sinf(lon * 5.0f + (float)d0) * 1.4f);
    const float guil = 0.86f + 0.14f * gl;
    const int ad = d0 < 0 ? -d0 : d0;
    const float today = ad == 0 ? 1.18f : 0.88f;
    float rgb[3];
    for (int q = 0; q < 3; ++q) rgb[q] = (float)((base >> (q * 8)) & 0xFF) * k * pool * guil * today;
    // a soft broad sheen from the lamp (no glint dot)
    const float sh = powf(dif, 10.0f) * 60.0f;
    for (int q = 0; q < 3; ++q) rgb[q] += sh;
    // the GOLD CLOISONS : a wire at each seam (its width in longitude : it thins toward the limb like a real wire)
    const float sd = (edge < 0.5f ? edge : 1.0f - edge) * band;
    const float wire = 0.06f;
    if (sd < wire) {
        const float g = 1.0f - sd / wire, gl2 = 0.50f + 0.85f * dif;
        const float GOLD[3] = { 0x40, 0xB0, 0xE8 };                     // B, G, R
        for (int q = 0; q < 3; ++q) rgb[q] = rgb[q] * (1.0f - g) + GOLD[q] * gl2 * g;
    }
    // a faint gilt rim (fresnel)
    const float fr = (1.0f - z) * (1.0f - z) * 0.35f;
    const float RIM[3] = { 0x50, 0xB8, 0xE8 };
    u32 o = 0xFF000000u;
    for (int q = 0; q < 3; ++q) {
        float c = rgb[q] + RIM[q] * fr;
        if (c > 255.0f) c = 255.0f; if (c < 0.0f) c = 0.0f;
        o |= (u32)c << (q * 8);
    }
    return o;
}
static void draw_day_sphere(u32 dev, float cx, float cy, float R, int di, float frac, float t) {
    PERF_SCOPE("mm.orb");
    const Lamp Lp = lamp_for(cx, cy);
    const u32 tg = ORB_GLOW[di] & 0x00FFFFFFu;
    // today's AURA, breathing (additive, under the orb)
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    const float br = 0.5f + 0.5f * sinf(t * 1.6f);
    disc_glow(dev, cx, cy, R * 0.92f, tg | ((u32)(40.0f + 40.0f * br) << 24), R * 0.30f);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    // THE MESH IN MERIDIANS : columns of constant view longitude from limb to limb, every 7.5 degrees PLUS, at each seam,
    // three columns (seam - wire, the seam, seam + wire) so the gold wire is a crisp geometric line and every slice a
    // clean colour (a polar grid interpolated the seams into zigzags). Rows : latitude, pole to pole.
    const float band = 0.7853982f, wire = 0.045f, HP = 1.5707963f;
    float col[64]; int nc = 0;
    for (int i = 0; i <= 24; ++i) col[nc++] = -HP + (float)i * (3.1415927f / 24.0f);
    for (int k = -4; k <= 4; ++k) {
        const float sp = ((float)k + 1.0f - frac) * band;              // a seam's view longitude (pp = k + 0.5)
        const float c3[3] = { sp - wire, sp, sp + wire };
        for (int q = 0; q < 3; ++q) if (c3[q] > -HP && c3[q] < HP && nc < 64) col[nc++] = c3[q];
    }
    for (int i = 1; i < nc; ++i) { const float vv = col[i]; int j = i - 1; while (j >= 0 && col[j] > vv) { col[j + 1] = col[j]; --j; } col[j + 1] = vv; }
    const int NLAT = 48;                                                // fine rows : a smooth silhouette, the guilloche resolves
    static VtxC orbV[64 * 2 * (NLAT + 1)]; static int orbN = 0; static MeshKey orbKey = {}; static bool orbOk = false;
    const MeshKey key = { { cx, cy, R, (float)di, floorf(frac * 1440.0f), Lp.x, Lp.y } };   // rebuilt once per Vana'diel minute
    if (mesh_key_changed(orbKey, key, orbOk)) {
        orbN = 0;
        for (int i = 0; i + 1 < nc; ++i) {
            if (col[i + 1] - col[i] < 1e-4f) continue;
            VtxC* strip = orbV + orbN * 2 * (NLAT + 1);
            for (int j = 0; j <= NLAT; ++j) {
                const float la = -HP + (float)j * (3.1415927f / NLAT), cl = cosf(la), sl = sinf(la);
                for (int q = 0; q < 2; ++q) {
                    const float ph = col[i + q], u = cl * sinf(ph), v = sl, zz = cl * cosf(ph);
                    strip[2 * j + q] = { cx - 0.5f + u * R, cy - 0.5f + v * R, 0, 1, orb_col(u, v, zz < 0.0f ? 0.0f : zz, di, frac, t, Lp) };
                }
            }
            ++orbN;
        }
    }
    for (int i = 0; i < orbN; ++i) dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * NLAT, orbV + i * 2 * (NLAT + 1), sizeof(VtxC));
    ring_sector(dev, cx, cy, R - 0.8f, R + 0.6f, 0.0f, 6.2832f, 0xA0080B10u);   // the limb, anti-aliased
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                         // (no specks of light : the user 2026-11-25)
    // the GLASS SHELL : a crescent reflection up left, a glint
    { const float la = atan2f(Lp.y, Lp.x); ring_sector(dev, cx, cy, R * 0.70f, R * 0.86f, la - 0.45f, la + 0.45f, 0x38FFFFFFu); }   // the reflection, on the lamp's side
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);                 // (no glint dot : too marked)
}

// A lit SPHERE over a polar grid (no seams to carry) : `colf(u, v, z)` gives each vertex's colour from its unit normal
// (u right, v down, z toward you). Shared by the moon and the glass domes. Colour-quad state.
// MESH CACHES (//aio perf 2026-11-25 : the Dial cost 2.0 ms a frame, half the HUD -- thousands of vertices whose colour
// is a few exp / atan2 / pow each, recomputed every frame although their inputs change once per Vana'diel minute) : each
// per-vertex-coloured mesh is built into a fixed CPU buffer and REBUILT only when its key changes (its screen place and
// size, the hour's minute, the phase, the lamp...) ; every frame only replays the strips. CPU memory, no GPU handle :
// nothing to forget on a device loss.
struct SphCache { MeshKey key = {}; bool valid = false; VtxC v[12 * 2 * 57]; };
template <class F> static void sphere_mesh_c(u32 dev, SphCache& C, const MeshKey& key, float cx, float cy, float R, F colf) {
    const int NR = 12, NS = 56;                                         // smooth silhouettes (a coarser mesh showed facets, the user 2026-11-25)
    if (mesh_key_changed(C.key, key, C.valid)) {
        for (int i = 0; i < NR; ++i) {
            const float r0 = (float)i / NR, r1 = (float)(i + 1) / NR;
            VtxC* strip = C.v + i * 2 * (NS + 1);
            for (int j = 0; j <= NS; ++j) {
                const float a = (float)j / NS * 6.2831853f, ca = cosf(a), sa = sinf(a);
                for (int q = 0; q < 2; ++q) {
                    const float rr = q == 0 ? r0 : r1, u = rr * ca, v = rr * sa;
                    float zz = 1.0f - u * u - v * v; zz = zz > 0.0f ? sqrtf(zz) : 0.0f;
                    strip[2 * j + q] = { cx - 0.5f + u * R, cy - 0.5f + v * R, 0, 1, colf(u, v, zz) };
                }
            }
        }
    }
    for (int i = 0; i < NR; ++i) dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * NS, C.v + i * 2 * (NS + 1), sizeof(VtxC));
}
template <class F> static void sphere_mesh(u32 dev, float cx, float cy, float R, F colf) {
    const int NR = 12, NS = 56;
    VtxC strip[2 * (NS + 1)];
    for (int i = 0; i < NR; ++i) {
        const float r0 = (float)i / NR, r1 = (float)(i + 1) / NR;
        for (int j = 0; j <= NS; ++j) {
            const float a = (float)j / NS * 6.2831853f, ca = cosf(a), sa = sinf(a);
            for (int q = 0; q < 2; ++q) {
                const float rr = q == 0 ? r0 : r1, u = rr * ca, v = rr * sa;
                float zz = 1.0f - u * u - v * v; zz = zz > 0.0f ? sqrtf(zz) : 0.0f;
                strip[2 * j + q] = { cx - 0.5f + u * R, cy - 0.5f + v * R, 0, 1, colf(u, v, zz) };
            }
        }
        dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * NS, strip, sizeof(VtxC));
    }
}
static u32 rgb_scale(u32 c, float k) {
    u32 o = 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) { float x = (float)((c >> sh) & 0xFF) * k; if (x > 255.0f) x = 255.0f; if (x < 0.0f) x = 0.0f; o |= (u32)x << sh; }
    return o;
}
// THE GLASS DOME over a subdial (the user 2026-11-25 : "spherical, glass reflections") : a faint fresnel rim, a crescent
// reflection up left. Drawn LAST over the face ; the blend is reset after its additive part.
static void glass_dome(u32 dev, float cx, float cy, float R, u32 rimTint, float k = 1.0f) {   // k < 1 over a LIGHT face (additive saturates)
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    auto A = [k](float a) { return (u32)(a * k) << 24; };
    ring_sector(dev, cx, cy, R * 0.90f, R, 0.0f, 6.2832f, (rimTint & 0x00FFFFFFu) | A(34.0f));   // the rim catches the light
    const Lamp Lp = lamp_for(cx, cy); const float la = atan2f(Lp.y, Lp.x);
    ring_sector(dev, cx, cy, R * 0.70f, R * 0.86f, la - 0.45f, la + 0.45f, 0x00FFFFFFu | A(48.0f));   // the crescent reflection, toward the lamp
    ring_sector(dev, cx, cy, R * 0.78f, R * 0.88f, la + 3.1416f - 0.40f, la + 3.1416f + 0.40f, 0x00FFFFFFu | A(18.0f));   // a weaker one, opposite
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);                                              // (no glint dot : too marked, the user 2026-11-25)
}
// THE MOON as a SPHERE : ivory with darker maria, lit by the REAL phase (the illuminated fraction f = moonPct ;
// waxing = lit from the right, waning = from the left, as build_moon_argb), a soft terminator, and the dark side's
// faint earthshine.
static void draw_moon_sphere(u32 dev, float cx, float cy, float R, int pct, bool waning) {
    float f = (float)pct / 100.0f; if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
    const float th = acosf(2.0f * f - 1.0f), sx = waning ? -1.0f : 1.0f;     // th = 0 full, pi new
    const float lx = sx * sinf(th), ly = -0.12f, lz = cosf(th);
    static const float MAR[5][4] = { {-0.30f,-0.25f,0.26f,0.30f}, {0.22f,-0.10f,0.20f,0.26f}, {-0.05f,0.28f,0.22f,0.22f}, {0.35f,0.35f,0.14f,0.20f}, {-0.42f,0.20f,0.12f,0.18f} };
    static SphCache cache; const MeshKey key = { { cx, cy, R, (float)pct, waning ? 1.0f : 0.0f } };
    sphere_mesh_c(dev, cache, key, cx, cy, R, [&](float u, float v, float z) -> u32 {
        float m = 1.0f;
        for (int k = 0; k < 5; ++k) { const float dx = u - MAR[k][0], dy = v - MAR[k][1]; m -= MAR[k][3] * expf(-(dx * dx + dy * dy) / (MAR[k][2] * MAR[k][2])); }
        float ndl = u * lx + v * ly + z * lz;
        float lit = (ndl + 0.06f) / 0.22f; if (lit < 0.0f) lit = 0.0f; if (lit > 1.0f) lit = 1.0f;   // a soft terminator
        lit = lit * lit * (3.0f - 2.0f * lit);
        const float shade = (0.55f + 0.55f * (ndl > 0.0f ? ndl : 0.0f)) * (0.70f + 0.30f * z);
        const u32 day = rgb_scale(0xFFEDE4CCu, m * shade);
        const u32 night = rgb_scale(0xFF1A2440u, 0.55f + 0.25f * m);           // earthshine
        u32 o = 0xFF000000u;
        for (int sh = 0; sh < 24; sh += 8) o |= (u32)((float)((night >> sh) & 0xFF) * (1.0f - lit) + (float)((day >> sh) & 0xFF) * lit) << sh;
        return o;
    });
    ring_sector(dev, cx, cy, R - 0.8f, R + 0.6f, 0.0f, 6.2832f, 0x90060A14u);   // the limb, anti-aliased
}
// THE DAY CAROUSEL (the user 2026-11-25 : "exactly like the sphere, but instead of the quarters, the icons") : the
// 8 elements' HD icons ride the equator of a dark glass globe that spins on its upright axis with the hours, with the
// sphere's own rule -- today's icon ENTERS the front at midnight, faces you (under the needle) at noon, leaves at the
// next midnight. An icon at longitude L is drawn at x = R' sin L, squashed horizontally by cos L (it lies on the
// sphere), shrunk and dimmed with its depth ; the back half is hidden ; drawn back to front. Needs the HD atlas.
static void draw_day_carousel(u32 dev, float cx, float cy, float R, int di, float frac, u32 wxTex) {
    PERF_SCOPE("mm.carousel");
    const Lamp Lp = lamp_for(cx, cy);
    // the dark glass globe behind the icons (cached : the lamp and the place only)
    // the dark glass globe, TODAY's element glowing inside it (the user 2026-11-25 : it lacked the globe's richness) :
    // the glow strongest at the heart, toward you, its light also catching the rim (cached : place, lamp, day)
    const u32 eg = ORB_JEWEL[di & 7];
    static SphCache gc; const MeshKey gk = { { cx, cy, R, Lp.x, Lp.y, (float)di, 8.0f } };
    sphere_mesh_c(dev, gc, gk, cx, cy, R, [Lp, eg](float u, float v, float z) -> u32 {
        float dif = Lp.x * u + Lp.y * v + Lp.z * z; if (dif < 0.0f) dif = 0.0f;
        const float k = (0.35f + 0.65f * dif) * (0.55f + 0.45f * z), fr = (1.0f - z) * (1.0f - z);
        const float core = z * z * 0.42f + fr * 0.30f;                     // the inner light, and its rim
        float c3[3];
        for (int q = 0; q < 3; ++q) { static const float GL[3] = { 0x1C, 0x16, 0x12 }; c3[q] = GL[q] * k + (float)((eg >> (q * 8)) & 0xFF) * core; if (c3[q] > 255.0f) c3[q] = 255.0f; }
        return 0xFF000000u | ((u32)c3[2] << 16) | ((u32)c3[1] << 8) | (u32)c3[0];
    });
    ring_sector(dev, cx, cy, R - 0.8f, R + 0.6f, 0.0f, 6.2832f, 0xA0080B10u);   // the limb, anti-aliased
    if (!wxTex) return;
    // the icons : day d (from today) at L = (d + 0.5 - frac) * 60 degrees -- WIDER than the sphere's 45 (the user : "cheat :
    // 3 icons at most, bigger") : at most three on the front ; one passing behind is simply the next day when it comes back
    struct It { float L, z; int cell; } it[8]; int n = 0;
    for (int d = -4; d <= 3; ++d) {
        const float L = ((float)d + 0.5f - frac) * 1.0471976f, z = cosf(L);
        if (z <= -0.60f) continue;                                         // wholly behind the globe (a partly visible one keeps its front cells)
        it[n].L = L; it[n].z = z; it[n].cell = (di + d + 16) & 7; ++n;
    }
    for (int i = 1; i < n; ++i) { It v = it[i]; int j = i - 1; while (j >= 0 && it[j].z > v.z) { it[j + 1] = it[j]; --j; } it[j + 1] = v; }   // back to front
    // each icon is MAPPED ONTO THE SPHERE (the user : "apply them on the sphere as a texture, for the perspective") : a
    // 10 x 10 grid spanning WL degrees of longitude x WL of latitude around its centre, every vertex placed on the globe
    // (x = R sin lon cos lat, y = -R sin lat), lit by the lamp from its normal, faded at the limb ; a cell turning past
    // the limb is dropped. The icon curves with the globe and foreshortens toward the edges.
    tex_state(dev, wxTex);
    const int NG = 10; const float WL = 1.30f, HL = 1.30f;               // ~75 degrees each way : big icons (spacing 60 : they may touch)
    static Vtx vb[8 * 6 * NG * NG]; int nv = 0;                         // EVERY icon can be (partly) in front : 8, not 3 (an overflow here wrote over the TUNE table, 2026-11-25)
    for (int i = 0; i < n; ++i) {
        float gx[NG + 1][NG + 1], gy[NG + 1][NG + 1], gz[NG + 1][NG + 1]; u32 gc[NG + 1][NG + 1];
        for (int r = 0; r <= NG; ++r) for (int q = 0; q <= NG; ++q) {
            const float lon = it[i].L + (((float)q / NG) - 0.5f) * WL, lat = (0.5f - (float)r / NG) * HL;
            const float cl = cosf(lat), u = sinf(lon) * cl, v = -sinf(lat), z = cosf(lon) * cl;
            gx[r][q] = cx + R * u; gy[r][q] = cy + R * v; gz[r][q] = z;
            float dif = Lp.x * u + Lp.y * v + Lp.z * z; if (dif < 0.0f) dif = 0.0f;
            const float lit = 0.45f + 0.75f * dif; const u32 g = (u32)(255.0f * (lit > 1.0f ? 1.0f : lit));
            float fa = z <= 0.0f ? 0.0f : (z < 0.55f ? z / 0.55f : 1.0f); fa = fa * fa * (3.0f - 2.0f * fa);   // a long smooth fade into the glass (no cut at the rim)
            gc[r][q] = ((u32)(255.0f * fa) << 24) | (g << 16) | (g << 8) | g;
        }
        const float u0 = it[i].cell / 8.0f, du = 1.0f / 8.0f;
        for (int r = 0; r < NG; ++r) for (int q = 0; q < NG; ++q) {
            if (gz[r][q] <= 0.0f || gz[r][q + 1] <= 0.0f || gz[r + 1][q] <= 0.0f || gz[r + 1][q + 1] <= 0.0f) continue;   // past the limb
            if (nv + 6 > (int)(sizeof(vb) / sizeof(vb[0]))) break;           // never past the buffer
            const Vtx A = { gx[r][q] - 0.5f,         gy[r][q] - 0.5f,         0, 1, gc[r][q],         u0 + du * q / NG,       (float)r / NG };
            const Vtx B = { gx[r][q + 1] - 0.5f,     gy[r][q + 1] - 0.5f,     0, 1, gc[r][q + 1],     u0 + du * (q + 1) / NG, (float)r / NG };
            const Vtx C = { gx[r + 1][q] - 0.5f,     gy[r + 1][q] - 0.5f,     0, 1, gc[r + 1][q],     u0 + du * q / NG,       (float)(r + 1) / NG };
            const Vtx D = { gx[r + 1][q + 1] - 0.5f, gy[r + 1][q + 1] - 0.5f, 0, 1, gc[r + 1][q + 1], u0 + du * (q + 1) / NG, (float)(r + 1) / NG };
            vb[nv++] = A; vb[nv++] = B; vb[nv++] = C; vb[nv++] = B; vb[nv++] = D; vb[nv++] = C;
        }
    }
    if (nv >= 3) dDrawUP(dev, D3DPT_TRIANGLELIST, (u32)(nv / 3), vb, sizeof(Vtx));
    dSetTex(dev, 0, 0);
    color_state(dev);
}
// THE DAY POD (the user 2026-11-25 : "think watchmaking -- how would a watch with such a module do it") : the case grows
// a HOUSING at the lower right, cast with it (it comes out from under the ring) ; a hidden wheel of the 8 elements turns
// inside, seen only through an arched WINDOW (a guichet) that shows three icons : yesterday, today, tomorrow. The window
// has a bevelled frame, a dark guilloche floor, a glass over it and a gilt INDEX at its centre ; two screws hold the
// housing at its ends. The icons stand upright and slide along the arc with the hours : today's is centred under the
// index at noon, enters at midnight from one end and leaves at the next by the other. Drawn BEFORE the frame and the map.
// the weather as the Dial shows it (res/weather.lua id ; the dev overrides mm.test_weather / mm.test_wcycle applied) and
// its ELEMENT in the day order (0 Fire .. 7 Dark ; -1 none : clear, sunshine, clouds, fog)
static int mm_weather(int real) {
    int wid = real;
    { const float tw = TUNE("mm.test_weather", -1.0f); if (tw >= 0.0f) wid = (int)tw; }
    { const float wc = TUNE("mm.test_wcycle", 0.0f);
      if (wc > 0.0f) { static const int WC[10] = { 1, 5, 7, 9, 2, 11, 13, 15, 17, 19 }; wid = WC[(int)(model_now_ms() / 1000.0 / wc) % 10]; } }
    return wid;
}
static int mm_weather_elem(int wid) {
    static const int WCELL[8] = { 0, 2, 1, 3, 4, 5, 6, 7 };              // weather pair (fire, water, earth, wind, ice, thunder, light, dark) -> day order
    return (wid >= 4 && wid <= 19) ? WCELL[(wid - 4) / 2] : -1;
}
// A DOMED METAL CAPSULE (the user 2026-11-25 : "back to metal, it was better -- a domed capsule, not much detail, a
// metal ground so the icon stands out") : the theme's metal, a shade darker than the case so a bright icon reads on it,
// domed by the instrument's lamp (lit on its side, in shade on the other), a polished lip. _under before the icon, _over after.
static void glass_bead_under(u32 dev, float x, float y, float R, float S) {
    const Lamp Lp = lamp_for(x, y); const float la = atan2f(Lp.y, Lp.x);
    disc(dev, x + 0.6f * S, y + 1.6f * S, R + 0.6f * S, 0x60000000u);              // its shadow
    disc(dev, x, y, R + 1.3f * S, g_dm.edge);                                    // the dark edge
    disc(dev, x, y, R, dm_toward(g_dm.body, 0xFF000000u, 0.30f));               // the dome, a shade darker than the case
    disc(dev, x + Lp.x * R * 0.22f, y + Lp.y * R * 0.22f, R * 0.68f, MA(0x55, g_dm.top));   // lit toward the lamp...
    ring_sector(dev, x, y, R * 0.70f, R, la + 3.1416f - 1.2f, la + 3.1416f + 1.2f, MA(0x70, g_dm.shade));   // ...in shade opposite
    ring_sector(dev, x, y, R - 1.1f * S, R, la - 1.3f, la + 1.3f, MA(0xB0, g_dm.lite));   // the polished lip
}
static void glass_bead_over(u32 dev, float x, float y, float R, float S) {
    const Lamp Lp = lamp_for(x, y);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    disc_glow(dev, x + Lp.x * R * 0.45f, y + Lp.y * R * 0.45f, R * 0.06f, MA(0x40, g_dm.glint), R * 0.22f);   // a soft sheen, no glint dot
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    (void)S;
}
// A POD'S HOUSING, MACHINED OUT OF THE BEZEL (the user 2026-11-25 : "they stop dead -- they must melt into the dial" ;
// then "think realism, steam magitech : it must be credible") : a BOSS milled in the ring's own metal, as a Bastokan
// instrument maker would cut it -- a flat top face, its two ends CHAMFERED at 45 degrees down into the ring (plane facets :
// the one facing the lamp, upper left, lit ; the other in shade), a sharp crease between face and chamfer, an engraved
// groove along the top (a tool mark). No screws (refused). The ring's rim, drawn later, overlaps its root.
static u32 pod_mix(u32 a, u32 b, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const u32 r = (u32)(((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t));
    const u32 g = (u32)(((a >> 8) & 0xFF) + (((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t));
    const u32 bl = (u32)((a & 0xFF) + (((int)(b & 0xFF) - (int)(a & 0xFF)) * t));
    return (a & 0xFF000000u) | (r << 16) | (g << 8) | bl;
}
// THE LAMP (upper left) and a surface's light : l = n . L in [-1, 1] for a surface whose normal is n
static const float POD_LX = -0.55f, POD_LY = -0.83f;
// a band of the boss lit CONTINUOUSLY by the lamp (the user 2026-11-25 : "the lighting is not finished") : per-vertex colour
// from the radial normal at each angle -- no hard step where the light turns. kind 0 : the front face (a slightly domed
// metal, +-14 %) ; kind 1 : the outer lip (glint toward the lamp, shade away from it).
static void pod_lit_band(u32 dev, float cx, float cy, float r0, float r1, float a0, float a1, u32 hb, int kind) {
    const int N = 32; VtxC v[(N + 1) * 2];
    for (int i = 0; i <= N; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / N, l = cosf(a) * POD_LX + sinf(a) * POD_LY;
        u32 c;
        if (kind == 0) c = l > 0.0f ? pod_mix(hb, g_dm.lite, l * 0.14f) : pod_mix(hb, g_dm.edge, -l * 0.14f);
        else           c = l > 0.0f ? MA((u32)(40.0f + 150.0f * l), g_dm.glint) : MA((u32)(40.0f - 40.0f * l * 2.0f), g_dm.shade);
        v[2 * i]     = { cx + cosf(a) * r0 - 0.5f, cy + sinf(a) * r0 - 0.5f, 0, 1, c };
        v[2 * i + 1] = { cx + cosf(a) * r1 - 0.5f, cy + sinf(a) * r1 - 0.5f, 0, 1, c };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, N * 2, v, sizeof(VtxC));
}
// THE MODULES' SLOTS (the user 2026-11-25 : "players choose where to put them") : the hour and the grid square at the top
// or the bottom (they alternate), the moon and the day's element in one of the four corners or on a side (never the same
// one) ; Minimap::draw sets the angles each frame from the config.
static const float POD_SLOT[6] = { -2.3562f, 3.14159265f, 2.3562f, 0.7854f, 0.0f, -0.7854f };   // NW, W, SW, SE, E, NE
static float s_dayTh = 0.7854f, s_moonTh = 2.3562f, s_hourTh = -1.5707963f, s_posTh = 1.5707963f, s_skyTh = -0.7854f;
static bool s_skyPod = false;   // the SKY module replaces the old sky dial and its weather capsules
// reading order : left to right, top to bottom on a side -- +1 when the increasing angle runs that way here
static float pod_dir(float th) { return (-sinf(th) + 0.5f * cosf(th)) >= 0.0f ? 1.0f : -1.0f; }
// a wheel's figure turned in its reading sense (head outward on the top half, toward the centre on the bottom half,
// never upside down) ; on a SIDE slot the figures stay upright (a quarter turn would lay them on their side)
static float pod_rot1(float a) {
    float r = a + (sinf(a) > 0.0f ? -1.5707963f : 1.5707963f);
    while (r > 3.14159265f) r -= 6.2831853f; while (r < -3.14159265f) r += 6.2831853f;
    return r;
}
static float pod_rot(float a, float thPod) { return fabsf(pod_rot1(thPod)) > 0.80f ? 0.0f : pod_rot1(a); }
static float pod_wrapa(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static float pod_smax(float a, float b, float k) { return 0.5f * (a + b + sqrtf((a - b) * (a - b) + k * k)); }
// the modules present this frame (Minimap::draw) : their angles and half-spans
static const int POD_MAX = 8;   // (was 4 : with the sky module the fifth -- the grid square -- was silently dropped, no tube, no weld)
static int s_podN = 0; static float s_podTh[POD_MAX], s_podPh[POD_MAX];

// THE COLLAR (the user 2026-11-25 : "join them at the weld ; adapt the welds to where the modules are") : ONE cast piece
// for every module present -- each a lug cast with the watch case (a flat top, a rounded corner, a steep wall, a concave
// fillet flush into the ring : "an adventurer's pocket watch forged in Bastok's metal, made by Cid") ; two modules close
// enough are WELDED by a lower bridge between them, its valleys rounded (a smooth max) -- the silhouette is computed from
// the modules present, wherever they sit. Lit continuously from the lamp ; a polished bevel along its edge.
static void pod_collar(u32 dev, float cx, float cy, float r, float S, u32 hb) {
    const int n = s_podN; if (n <= 0) return;
    const float D = r * TUNE("mm.pod_depth", 0.235f), Ri = r - 3.0f * S, Ro = r + D, rB = r - 1.5f * S, H = Ro - rB, rm = (Ri + Ro) * 0.5f;
    const float L = H * TUNE("mm.pod_lug", 1.05f) / rm, kS = TUNE("mm.pod_lug_k", 5.0f), tk = tanhf(kS * 0.5f);
    const float bR = rB + H * TUNE("mm.pod_bridge_h", 1.0f), bMax = TUNE("mm.pod_bridge", 2.4f) * L, kR = 3.0f * S;   // full height : two neighbours JOIN, no notch (the user 2026-11-25)
    float bA0[POD_MAX * 2], bLen[POD_MAX * 2]; int nb = 0;                                    // the welds : from a module's end to the next one's start
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {
        if (i == j || nb >= POD_MAX * 2) continue;
        float g = pod_wrapa((s_podTh[j] - s_podPh[j]) - (s_podTh[i] + s_podPh[i])); if (g < 0.0f) g += 6.2831853f;
        if (g <= bMax) { bA0[nb] = s_podTh[i] + s_podPh[i]; bLen[nb] = g; ++nb; }
    }
    auto prof = [&](float a) {
        float best = rB;
        for (int i = 0; i < n; ++i) {
            const float u = fabsf(pod_wrapa(a - s_podTh[i]));
            float p;
            if (u <= s_podPh[i]) p = Ro;
            else { const float t = (u - s_podPh[i]) / L; p = t >= 1.0f ? rB : rB + H * (0.5f - 0.5f * tanhf(kS * (t - 0.5f)) / tk); }
            if (p > best) best = p;
        }
        for (int q = 0; q < nb; ++q) {
            float d = pod_wrapa(a - bA0[q]); if (d < 0.0f) d += 6.2831853f;
            if (d <= bLen[q]) best = bR >= Ro - 0.01f ? fmaxf(best, bR) : pod_smax(best, bR, kR);
        }
        return best;
    };
    const int N = 360;
    static float Ra[N + 1];
    for (int i = 0; i <= N; ++i) Ra[i] = prof(-3.14159265f + 6.2831853f * (float)i / N);
    static VtxC buf[N * 6];
    // one layer : a band from inner(a, R) to outer(a, R), coloured per vertex, over every segment where the collar stands
    auto band = [&](float ox, float oy, float (*inner)(float, float, float), float (*outer)(float, float, float), float prm, u32 (*colf)(float, float, float, u32), u32 base) {
        int m = 0;
        for (int i = 0; i < N; ++i) {
            if (Ra[i] < rB + 0.3f && Ra[i + 1] < rB + 0.3f) continue;
            const float a0 = -3.14159265f + 6.2831853f * (float)i / N, a1 = -3.14159265f + 6.2831853f * (float)(i + 1) / N;
            const float i0 = inner(Ra[i], prm, S), o0 = outer(Ra[i], prm, S), i1 = inner(Ra[i + 1], prm, S), o1 = outer(Ra[i + 1], prm, S);
            const u32 c0i = colf(a0, Ra[i], 0.0f, base), c0o = colf(a0, Ra[i], 1.0f, base), c1i = colf(a1, Ra[i + 1], 0.0f, base), c1o = colf(a1, Ra[i + 1], 1.0f, base);
            const VtxC A = { cx + ox + cosf(a0) * i0 - 0.5f, cy + oy + sinf(a0) * i0 - 0.5f, 0, 1, c0i }, B = { cx + ox + cosf(a0) * o0 - 0.5f, cy + oy + sinf(a0) * o0 - 0.5f, 0, 1, c0o };
            const VtxC C = { cx + ox + cosf(a1) * i1 - 0.5f, cy + oy + sinf(a1) * i1 - 0.5f, 0, 1, c1i }, E = { cx + ox + cosf(a1) * o1 - 0.5f, cy + oy + sinf(a1) * o1 - 0.5f, 0, 1, c1o };
            buf[m++] = A; buf[m++] = B; buf[m++] = C; buf[m++] = B; buf[m++] = E; buf[m++] = C;
        }
        if (m >= 3) dDrawUP(dev, D3DPT_TRIANGLELIST, m / 3, buf, sizeof(VtxC));
    };
    struct K { static float lipIn(float R, float p, float s) { (void)p; return R - 1.6f * s; }
               static float lipOut(float R, float p, float s) { (void)p; (void)s; return R; }
               static float bv0(float R, float p, float s) { (void)p; return R - 3.6f * s; }
               static float bv1(float R, float p, float s) { (void)p; return R - 1.8f * s; }
               static float sh0(float R, float p, float s) { (void)p; return R - 5.4f * s; }
               static float sh1(float R, float p, float s) { (void)p; return R - 3.6f * s; } };
    color_state(dev);
    // its shadow, its dark edge
    {
        struct L1 { static u32 c(float, float, float, u32 b) { return b; } };
        static float s_ri, s_g;
        s_ri = Ri - 0.6f * S; s_g = 0.6f * S;
        struct G1 { static float in(float R, float p, float s) { (void)R; (void)s; return p; } static float out(float R, float p, float s) { (void)p; (void)s; return R + s_g; } };
        band(0.8f * S, 1.4f * S, G1::in, G1::out, s_ri, L1::c, 0x60000000u);
        s_ri = Ri - 1.3f * S; s_g = 1.3f * S;
        band(0.0f, 0.0f, G1::in, G1::out, s_ri, L1::c, g_dm.edge);
    }
    // the cast body : lit from the lamp, darker where it falls away (the lugs' walls, the welds)
    {
        static float s_Ro, s_H;
        s_Ro = Ro; s_H = H;
        struct B1 { static u32 c(float a, float R, float, u32 b) {
            const float l = cosf(a) * POD_LX + sinf(a) * POD_LY;
            u32 cc = l > 0.0f ? pod_mix(b, g_dm.lite, l * 0.14f) : pod_mix(b, g_dm.edge, -l * 0.14f);
            return pod_mix(cc, g_dm.edge, 0.20f * fminf(1.0f, (s_Ro - R) / s_H)); } };
        struct G2 { static float in(float R, float p, float s) { (void)R; (void)s; return p; } static float out(float R, float p, float s) { (void)p; (void)s; return R; } };
        band(0.0f, 0.0f, G2::in, G2::out, Ri, B1::c, hb);
    }
    // the rounded cast edge : a soft lit lip toward the lamp, in shade away from it
    {
        struct C1 { static u32 c(float a, float, float t, u32) {
            const float l = cosf(a) * POD_LX + sinf(a) * POD_LY;
            const u32 cc = l > 0.0f ? MA((u32)(40.0f + 150.0f * l), g_dm.glint) : MA((u32)(40.0f - 80.0f * l), g_dm.shade);
            return t > 0.5f ? cc : (cc & 0x00FFFFFFu); } };
        band(0.0f, 0.0f, K::lipIn, K::lipOut, 0.0f, C1::c, 0);
    }
    // the polished bevel inside the edge : a bright band, a soft shadow below it
    {
        struct C2 { static u32 c(float a, float, float t, u32) {
            const float l = cosf(a) * POD_LX + sinf(a) * POD_LY;
            const u32 hi = MA((u32)(30.0f + 70.0f * (l > 0.0f ? l : 0.0f)), g_dm.lite);
            return t > 0.5f ? hi : (hi & 0x00FFFFFFu); } };
        struct C3 { static u32 c(float, float, float t, u32) { return t > 0.5f ? MA(0x30, g_dm.edge) : 0x00000000u; } };
        band(0.0f, 0.0f, K::sh0, K::sh1, 0.0f, C3::c, 0);
        band(0.0f, 0.0f, K::bv0, K::bv1, 0.0f, C2::c, 0);
    }
    // the outline, anti-aliased (the strips' own edges are hard)
    for (int i = 0; i < N; ++i) {
        if (Ra[i] < rB + 0.3f && Ra[i + 1] < rB + 0.3f) continue;
        const float a0 = -3.14159265f + 6.2831853f * (float)i / N, a1 = -3.14159265f + 6.2831853f * (float)(i + 1) / N;
        const float r0 = Ra[i] + 1.3f * S, r1 = Ra[i + 1] + 1.3f * S;
        seg_soft(dev, cx + cosf(a0) * r0, cy + sinf(a0) * r0, cx + cosf(a1) * r1, cy + sinf(a1) * r1, 1.2f * S, g_dm.edge);
    }
}
void clip_rect_begin(u32 dev, float x, float y, float w, float h);   // (config_controls.cpp : a sub-viewport clip)
void clip_rect_end(u32 dev);
// THE WHEELS' LOOK (the user 2026-11-25 : "the same quality as the hour's wheels -- it must really feel like wheels") : each
// cell is drawn as drum_text draws a wheel, in the cell's own frame -- parchment, a bright crown, the paper TURNING AWAY
// toward its two edges along the roll (outer and inner, the radial axis), its rounded sides ; hairline cuts between the
// wheels (a dark cut, a lit edge) ; a glass over the window (a sheen on its outer half, a lit lip).
static u32 col_lerp(u32 a, u32 b, float t) {
    u32 o = 0;
    for (int sh = 0; sh < 32; sh += 8) { const float x = (float)((a >> sh) & 0xFF), y = (float)((b >> sh) & 0xFF); o |= ((u32)(x + (y - x) * t + 0.5f) & 0xFF) << sh; }
    return o;
}
static void sector_grad(u32 dev, float cx, float cy, float r0, float r1, float a0, float a1, u32 cA0r0, u32 cA0r1, u32 cA1r0, u32 cA1r1) {
    const int N = 8; VtxC v[(N + 1) * 2];
    for (int i = 0; i <= N; ++i) {
        const float t = (float)i / N, a = a0 + (a1 - a0) * t;
        v[2 * i]     = { cx + cosf(a) * r0 - 0.5f, cy + sinf(a) * r0 - 0.5f, 0, 1, col_lerp(cA0r0, cA1r0, t) };
        v[2 * i + 1] = { cx + cosf(a) * r1 - 0.5f, cy + sinf(a) * r1 - 0.5f, 0, 1, col_lerp(cA0r1, cA1r1, t) };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, N * 2, v, sizeof(VtxC));
}
static void pod_wheel_cell(u32 dev, float cx, float cy, float rw, float wr, float a, float as) {   // one wheel, as wide as `as` (rad)
    const u32 sh = 0xB03A2410u, sh0 = 0x003A2410u, sd = 0x603A2410u;
    const float a0 = a - as * 0.5f, a1 = a + as * 0.5f;
    ring_sector(dev, cx, cy, rw - wr, rw + wr, a0, a1, 0xFFD6BC86u);                                   // the wheel's parchment
    ring_sector(dev, cx, cy, rw - wr * 0.52f, rw + wr * 0.52f, a - as * 0.36f, a + as * 0.36f, 0xFFEBD8AAu);   // its bright crown
    sector_grad(dev, cx, cy, rw + wr * 0.36f, rw + wr, a0, a1, sh0, sh, sh0, sh);                      // turning away : outward...
    sector_grad(dev, cx, cy, rw - wr, rw - wr * 0.36f, a0, a1, sh, sh0, sh, sh0);                      // ...and inward
    sector_grad(dev, cx, cy, rw - wr, rw + wr, a0, a0 + as * 0.16f, sd, sd, sh0, sh0);                 // its rounded sides
    sector_grad(dev, cx, cy, rw - wr, rw + wr, a1 - as * 0.16f, a1, sh0, sh0, sd, sd);
}
static void pod_wheel_shade(u32 dev, float cx, float cy, float rw, float wr, float a, float as) {   // a wheel's turning-away shade alone
    const u32 sh = 0xB03A2410u, sh0 = 0x003A2410u, sd = 0x603A2410u;
    const float a0 = a - as * 0.5f, a1 = a + as * 0.5f;
    sector_grad(dev, cx, cy, rw + wr * 0.36f, rw + wr, a0, a1, sh0, sh, sh0, sh);
    sector_grad(dev, cx, cy, rw - wr, rw - wr * 0.36f, a0, a1, sh, sh0, sh, sh0);
    sector_grad(dev, cx, cy, rw - wr, rw + wr, a0, a0 + as * 0.16f, sd, sd, sh0, sh0);
    sector_grad(dev, cx, cy, rw - wr, rw + wr, a1 - as * 0.16f, a1, sh0, sh0, sd, sd);
}
static void pod_cut(u32 dev, float cx, float cy, float rw, float wr, float sa, float S) {   // a cut between two wheels : a dark hairline, a lit edge
    const float tx = -sinf(sa), ty = cosf(sa);
    const float x0 = cx + cosf(sa) * (rw - wr), y0 = cy + sinf(sa) * (rw - wr), x1 = cx + cosf(sa) * (rw + wr), y1 = cy + sinf(sa) * (rw + wr);
    seg_soft(dev, x0, y0, x1, y1, 0.8f * S, 0xD0140C04u);
    seg_soft(dev, x0 + tx * 0.8f * S, y0 + ty * 0.8f * S, x1 + tx * 0.8f * S, y1 + ty * 0.8f * S, 0.5f * S, 0x40FFF0D0u);
}
static void pod_wheels_under(u32 dev, float cx, float cy, float rw, float wr, float th0, float as) {
    color_state(dev);
    for (int d = -1; d <= 1; ++d) pod_wheel_cell(dev, cx, cy, rw, wr, th0 + (float)d * as, as);
}
static void pod_wheels_over(u32 dev, float cx, float cy, float rw, float wr, float th0, float as, float Wh, float S) {
    color_state(dev);
    for (int e = -1; e <= 1; e += 2) {                                    // the cuts between the wheels : a dark hairline, a lit edge
        const float sa = th0 + (float)e * as * 0.5f, tx = -sinf(sa), ty = cosf(sa);
        const float x0 = cx + cosf(sa) * (rw - wr), y0 = cy + sinf(sa) * (rw - wr), x1 = cx + cosf(sa) * (rw + wr), y1 = cy + sinf(sa) * (rw + wr);
        seg_soft(dev, x0, y0, x1, y1, 0.8f * S, 0xD0140C04u);
        seg_soft(dev, x0 + tx * 0.8f * S, y0 + ty * 0.8f * S, x1 + tx * 0.8f * S, y1 + ty * 0.8f * S, 0.5f * S, 0x40FFF0D0u);
    }
    (void)Wh;                                                             // (the glass and its caps : pod_glass / pod_caps, after the housing's cover)
}
// THE GLASS TUBE AND ITS END CAPS (the user 2026-11-25 : "the glass tubes, their caps at the ends, the lighting -- not
// finished") : the wheels turn inside a GLASS CYLINDER lying along the arc ; across it (the radial axis) the glass is
// darker at its inner edge (it curves away), brighter at its outer edge, with ONE thin specular line where the lamp
// catches the curve -- stronger on the lamp's side ; at each end a turned METAL CAP (a ferrule) clamps the tube : a
// cylinder across the radius, lit on its outer half, a dark seam against the glass.
static void pod_glass(u32 dev, float cx, float cy, float rw, float wr, float a0, float a1, float S) {
    (void)S;
    color_state(dev);
    sector_grad(dev, cx, cy, rw - wr, rw - wr * 0.45f, a0, a1, 0x58000000u, 0x00000000u, 0x58000000u, 0x00000000u);   // the inner edge curves away : darker
    sector_grad(dev, cx, cy, rw + wr * 0.50f, rw + wr, a0, a1, 0x00FFFFFFu, 0x30FFFFFFu, 0x00FFFFFFu, 0x30FFFFFFu);   // the outer edge catches the sky
    const float l0 = cosf(a0) * POD_LX + sinf(a0) * POD_LY, l1 = cosf(a1) * POD_LX + sinf(a1) * POD_LY;
    const u32 s0 = MA((u32)(22.0f + 30.0f * (l0 > 0.0f ? l0 : 0.0f)), 0xFFFFFFu), s1 = MA((u32)(22.0f + 30.0f * (l1 > 0.0f ? l1 : 0.0f)), 0xFFFFFFu);   // subtle : it must never strike the figures out
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    sector_grad(dev, cx, cy, rw + wr * 0.70f, rw + wr * 0.80f, a0, a1, s0, s0, s1, s1);                       // the specular line, near the outer edge (clear of the figures)
    sector_grad(dev, cx, cy, rw + wr * 0.55f, rw + wr * 0.70f, a0, a1, 0, s0, 0, s1);                          // its soft halo (inner side)
    sector_grad(dev, cx, cy, rw - wr * 0.62f, rw - wr * 0.58f, a0, a1, 0x18FFFFFFu, 0x18FFFFFFu, 0x18FFFFFFu, 0x18FFFFFFu);   // a faint second reflection
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    color_state(dev);
}
static void pod_caps(u32 dev, float cx, float cy, float rw, float wr, float a0, float a1, float S) {
    color_state(dev);
    const float cw = 3.0f * S / rw, ri = rw - wr - 1.2f * S, ro = rw + wr + 1.2f * S;
    const u32 body = g_dm.bot | 0xFF000000u;
    for (int e = -1; e <= 1; e += 2) {
        const float aIn = e < 0 ? a0 : a1;                                // the glass's end
        const float c0 = e < 0 ? aIn - cw : aIn, c1 = c0 + cw;            // wholly OUTSIDE the window : it must not bite the end wheels
        const float l = cosf(aIn) * POD_LX + sinf(aIn) * POD_LY;           // the cap's own light : its outward face
        ring_sector(dev, cx, cy, ri - 1.0f * S, ro + 1.0f * S, c0 - 1.0f * S / rw, c1 + 1.0f * S / rw, g_dm.edge);   // its dark edge
        ring_sector(dev, cx, cy, ri, ro, c0, c1, l > 0.0f ? pod_mix(body, g_dm.lite, l * 0.25f) : pod_mix(body, g_dm.edge, -l * 0.25f));
        sector_grad(dev, cx, cy, rw, ro, c0, c1, MA(0x00, g_dm.lite), MA(0xC0, g_dm.lite), MA(0x00, g_dm.lite), MA(0xC0, g_dm.lite));   // a cylinder : lit on its outer half...
        sector_grad(dev, cx, cy, ri, rw, c0, c1, MA(0xA0, g_dm.edge), MA(0x00, g_dm.edge), MA(0xA0, g_dm.edge), MA(0x00, g_dm.edge));   // ...in shade on its inner
        ring_sector(dev, cx, cy, ro - 0.9f * S, ro, c0, c1, MA(0xD0, g_dm.glint));                    // the lip catching the light
        const float sa = e < 0 ? c1 : c0;                                  // the seam against the glass
        seg_soft(dev, cx + cosf(sa) * (rw - wr), cy + sinf(sa) * (rw - wr), cx + cosf(sa) * (rw + wr), cy + sinf(sa) * (rw + wr), 0.9f * S, 0xC0000000u);
    }
}
// A POD CELL THAT ROLLS (the user 2026-11-25 : "the hour's wheel system : three cells that change together" ; "the wheels
// must turn the way they are mounted -- along their axis to the centre") : each cell is a wheel whose axle lies ACROSS the
// radius, so it rolls ALONG the radius (outward = away from the dial's centre) ; the icons stay upright on the screen. The
// cell is a box in the wheel's own frame (radial u, tangential t) : the upright icon quad is clipped against it (convex
// polygon clip, UVs carried), so a tilted wheel cuts its icons along its own edges. k : 0..1 through the roll.
static void pod_roll_cell(u32 dev, float ccx, float ccy, float ang, float rc, float sz, float hT, float hR, float k, int oldCell, int newCell, u32 colOld, u32 colNew, float thPod) {
    const float ux = cosf(ang), uy = sinf(ang), tx = -uy, ty = ux;        // radial, tangential
    const float Cx = ccx + ux * rc, Cy = ccy + uy * rc;                   // the cell's centre
    auto draw = [&](float off, int c, u32 col, bool clip) {
        if (c < 0) return;
        const float px = Cx + ux * off, py = Cy + uy * off;
        const float u0 = c / 8.0f, u1 = (c + 1) / 8.0f;
        (void)thPod; const float rot = 0.0f, rc_ = cosf(rot), rs_ = sinf(rot), h2 = sz * 0.5f;   // ALWAYS UPRIGHT to the player's eye (the user 2026-11-25), whatever the wheel's tilt or turn
        const float cxq[4] = { -h2, h2, h2, -h2 }, cyq[4] = { -h2, -h2, h2, h2 }, uq[4] = { u0, u1, u1, u0 }, vq[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        float P[16][4];
        for (int q = 0; q < 4; ++q) { P[q][0] = px + cxq[q] * rc_ - cyq[q] * rs_; P[q][1] = py + cxq[q] * rs_ + cyq[q] * rc_; P[q][2] = uq[q]; P[q][3] = vq[q]; }
        int n = 4;
        if (clip) {
            const float pl[4][3] = { { ux, uy, hR }, { -ux, -uy, hR }, { tx, ty, hT }, { -tx, -ty, hT } };   // dot(p - C, n) <= h
            for (int q = 0; q < 4 && n > 0; ++q) {
                float Q[16][4]; int m = 0;
                for (int i = 0; i < n; ++i) {
                    const float* A = P[i]; const float* B = P[(i + 1) % n];
                    const float da = (A[0] - Cx) * pl[q][0] + (A[1] - Cy) * pl[q][1] - pl[q][2];
                    const float db = (B[0] - Cx) * pl[q][0] + (B[1] - Cy) * pl[q][1] - pl[q][2];
                    if (da <= 0.0f && m < 16) { for (int j = 0; j < 4; ++j) Q[m][j] = A[j]; ++m; }
                    if ((da <= 0.0f) != (db <= 0.0f) && m < 16) { const float t = da / (da - db); for (int j = 0; j < 4; ++j) Q[m][j] = A[j] + (B[j] - A[j]) * t; ++m; }
                }
                n = m; for (int i = 0; i < n; ++i) for (int j = 0; j < 4; ++j) P[i][j] = Q[i][j];
            }
        }
        if (n < 3) return;
        Vtx v[16];
        for (int i = 0; i < n; ++i) v[i] = { P[i][0] - 0.5f, P[i][1] - 0.5f, 0, 1, col, P[i][2], P[i][3] };
        dDrawUP(dev, D3DPT_TRIANGLEFAN, n - 2, v, sizeof(Vtx));
    };
    if (k >= 1.0f || oldCell < 0) { draw(0.0f, newCell, colNew, false); return; }
    const float e = 1.0f - (1.0f - k) * (1.0f - k), ch = 2.0f * hR;      // ease-out : it settles into its notch
    draw(e * ch, oldCell, colOld, true);                                  // the old value rolls OUT (away from the centre)...
    draw(-(1.0f - e) * ch, newCell, colNew, true);                        // ...the new one rolls in from the centre's side
}
// THE PRESENT MOON, INKED TO ITS EXACT PHASE (the user 2026-11-25 : "the current moon with precision -- we are at 62 %") :
// the lit part is the bare paper ; the dark part is ink, from the dark limb to the TERMINATOR, the ellipse x = (1 - 2 f) R
// sqrt(1 - y^2 / R^2) (f the lit fraction) ; waxing lit on the right, waning on the left (the plate's convention).
// Drawn in horizontal slices (any shape, crescent or gibbous), the terminator stroked soft, a fine inked outline, a light
// inked limb so a full moon still reads as a moon.
static float s_inkRotC = 1.0f, s_inkRotS = 0.0f;   // the present moon turned with its wheel (draw_moon_pod sets it)
static void draw_ink_moon(u32 dev, float x, float y, float R, float f, bool waning, float S,
                          float ax = 1.0f, float ay = 0.0f, float phi = 0.0f, float Rcyl = 0.0f, float Rwin = 0.0f, u32 alpha = 0xFF) {
    // ON A DRUM : (ax, ay) the wheel's roll axis, phi its turn ; a point at d along the axis sits at Rcyl sin(phi + d / Rcyl),
    // and what passes the window's edge (+-Rwin) is gone under the frame (pinned at the edge, the alpha fades it)
    auto T = [&](float lx0, float ly0, float& ox, float& oy) {
        const float lx = lx0 * s_inkRotC - ly0 * s_inkRotS, ly = lx0 * s_inkRotS + ly0 * s_inkRotC;   // turned with its wheel
        const float d = lx * ax + ly * ay;
        float d2 = d;
        if (Rcyl > 0.0f) {
            const float th = phi + d / Rcyl;
            d2 = fabsf(th) >= 1.5707963f ? (th > 0.0f ? Rwin : -Rwin) : Rcyl * sinf(th);
            d2 = d2 > Rwin ? Rwin : (d2 < -Rwin ? -Rwin : d2);
        }
        ox = x + lx + ax * (d2 - d); oy = y + ly + ay * (d2 - d);
    };
    // REAL INK ON PAPER (the user 2026-11-25 : "too sharp, too opaque") : a sepia wash, not a solid -- the paper shows
    // through, the wash a little uneven from row to row, soft edges
    const u32 ink = ((alpha * 0xB4u / 255u) << 24) | 0x003C2614u;
    auto ma = [&](u32 c) { return (((c >> 24) * alpha / 255u) << 24) | (c & 0x00FFFFFFu); };
    auto annulus = [&](float r0, float r1, u32 col) {
        if (Rcyl <= 0.0f) { ring_sector(dev, x, y, r0, r1, 0.0f, 6.2831853f, col); return; }   // at rest : the feathered ring
        const int M = 40; VtxC w[(M + 1) * 2];
        for (int i = 0; i <= M; ++i) {
            const float t = 6.2831853f * (float)i / M, c = cosf(t), sn = sinf(t);
            float px, py; T(c * r0, sn * r0, px, py); w[2 * i] = { px - 0.5f, py - 0.5f, 0, 1, col };
            T(c * r1, sn * r1, px, py); w[2 * i + 1] = { px - 0.5f, py - 0.5f, 0, 1, col };
        }
        dDrawUP(dev, D3DPT_TRIANGLESTRIP, M * 2, w, sizeof(VtxC));
    };
    const int N = 28; VtxC v[(N + 1) * 2]; float tx[N + 1], ty[N + 1];
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    for (int i = 0; i <= N; ++i) {
        const float yy = -R + 2.0f * R * (float)i / N, sq = sqrtf(fmaxf(0.0f, 1.0f - (yy * yy) / (R * R)));
        const float xl = -R * sq, xt = (1.0f - 2.0f * f) * R * sq;       // waxing : dark from the LEFT limb to the terminator
        const float a = waning ? -xt : xl, b = waning ? R * sq : xt;
        float px, py;
        const u32 wa = (u32)((float)(ink >> 24) * (0.84f + 0.16f * (float)((i * 7 + 3) % 5) / 4.0f));   // the wash, uneven
        const u32 ci = (wa << 24) | (ink & 0x00FFFFFFu);
        T(a, yy, px, py); v[2 * i]     = { px - 0.5f, py - 0.5f, 0, 1, ci };
        T(b, yy, px, py); v[2 * i + 1] = { px - 0.5f, py - 0.5f, 0, 1, ci };
        T(waning ? -xt : xt, yy, tx[i], ty[i]);
    }
    color_state(dev);
    annulus(R * 0.78f, R, ma(0x20603A10u));                               // the limb, lightly inked (the sphere)
    annulus(R * 0.90f, R, ma(0x20603A10u));
    if (f < 0.995f) dDrawUP(dev, D3DPT_TRIANGLESTRIP, N * 2, v, sizeof(VtxC));
    if (f > 0.005f && f < 0.995f)
        for (int i = 0; i < N; ++i) seg_soft(dev, tx[i], ty[i], tx[i + 1], ty[i + 1], 1.6f * S, ma(0x40000000u | 0x3C2614u));   // the terminator : the wash bleeds a little
    if (Rcyl <= 0.0f) ring_sector(dev, x, y, R - 0.8f * S, R + 0.2f * S, 0.0f, 6.2831853f, ma(0xB0000000u | 0x3C2614u));   // the inked outline, a fine pen line
    else {   // (rolling : the outline follows the drum)
        float px0 = 0.0f, py0 = 0.0f;
        for (int i = 0; i <= 48; ++i) {
            const float t = 6.2831853f * (float)i / 48.0f; float px, py; T(cosf(t) * (R - 0.3f * S), sinf(t) * (R - 0.3f * S), px, py);
            if (i) seg_soft(dev, px0, py0, px, py, 1.0f * S, ma(0xB0000000u | 0x3C2614u));
            px0 = px; py0 = py;
        }
    }
}
struct PodGeo { float th0, D, Ri, Ro, rm, isz, rw, wr, as, Wh, Ph; };
static PodGeo pod_geo(float r, float S) {
    PodGeo g;
    g.th0 = s_dayTh;                                                     // the day's element : its slot (Minimap > Clock, the user 2026-11-25)
    g.D = r * TUNE("mm.pod_depth", 0.235f);                              // the designer's spec (2026-11-25) : ~0.23 r at the summit
    g.Ri = r - 3.0f * S; g.Ro = r + g.D; g.rm = (g.Ri + g.Ro) * 0.5f;
    g.isz = r * TUNE("mm.pod_icon", 0.18f); g.rw = r + g.D * 0.50f; g.wr = g.isz * 0.56f;   // today's icon 0.18 r ; the window just holds it
    g.as = r * TUNE("mm.pod_step", 0.21f) / g.rw; g.Wh = 1.5f * g.as; g.Ph = g.Wh + 2.6f * S / g.rm + 0.04f;   // centres 0.21 r apart
    return g;
}
// the NEEDLE of the day pod : it rises out of the compass's N (the user 2026-11-25 : "the needle must start from the N")
// up to today's icon -- drawn OVER the ring (the pod itself is under it), before the cardinal letters
static void draw_day_pod_needle(u32 dev, float cx, float cy, float rc, float r, float S, float csz) {
    const PodGeo g = pod_geo(r, S);
    const float ux = cosf(g.th0), uy = sinf(g.th0), nx = -uy, ny = ux;
    const float rN = (rc + r) * 0.5f;                                    // the N's centre radius
    const float r0 = rN - csz * 0.10f, r1 = g.rw + g.wr * 0.12f - g.isz * 0.44f;   // from INSIDE the N (drawn over its root) to the foot of today's icon
    const float x0 = cx + ux * r0, y0 = cy + uy * r0, x1 = cx + ux * r1, y1 = cy + uy * r1, w = 2.6f * S;
    color_state(dev);
    const float sh[6] = { x0 + nx * w + 0.8f, y0 + ny * w + 1.0f, x0 - nx * w + 0.8f, y0 - ny * w + 1.0f, x1 + 0.8f, y1 + 1.0f };
    fill_poly_aa(dev, sh, 3, 0x80000000u);                               // its shadow
    const float nd[6] = { x0 + nx * w, y0 + ny * w, x0 - nx * w, y0 - ny * w, x1, y1 };
    fill_poly_aa(dev, nd, 3, 0xFFE0452Fu);                               // the N's red, a compass needle
    const float hl[6] = { x0, y0, x0 - nx * w * 0.85f, y0 - ny * w * 0.85f, x1, y1 };
    fill_poly_aa(dev, hl, 3, 0x70FFFFFFu);                               // its lit facet
}
static void draw_day_disc(u32 dev, float cx, float cy, float r, float S, int di, float frac, u32 wxTex, int wElem) {
    (void)wElem;                                                         // (no resonance mark : dropped, the user 2026-11-25)
    PERF_SCOPE("mm.disc");
    const PodGeo G = pod_geo(r, S);                                      // above the North : yesterday | today | tomorrow, left to right
    const float PI = 3.14159265f, th0 = G.th0, D = G.D, Ri = G.Ri, Ro = G.Ro, rm = G.rm, capR = (Ro - Ri) * 0.5f;
    const float isz = G.isz, rw = G.rw, wr = G.wr, as = G.as, Wh = G.Wh, Ph = G.Ph;
    (void)D;
    auto at = [&](float ang, float rad, float& x, float& y) { x = cx + cosf(ang) * rad; y = cy + sinf(ang) * rad; };
    auto housing = [&](float grow, u32 col) {                             // the housing : a straight-cut arc (no round ends, the user 2026-11-25)
        ring_sector(dev, cx, cy, Ri - grow, Ro + grow, th0 - Ph - grow / rm, th0 + Ph + grow / rm, col);
    };
    color_state(dev);
    // ---- the housing, cast into the dial : its ends flow down into the ring ----
    const u32 hb = dm_toward(g_dm.bot, 0xFF000000u, TUNE("mm.pod_dark", 0.25f));   // the ring's own metal : one cast piece
    (void)Ri; (void)Ro;                                                  // (the housing : the collar, drawn once for all the modules)
    (void)housing;
    // ---- the WINDOW : a chamfer, then the dark guilloche floor ----
    ring_sector(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 - Wh - 0.02f, th0 + Wh + 0.02f, g_dm.dark);
    // the wheel is PAPER (the user 2026-11-25 : "as if inside it were paper, separations, icons painted on") : the map's
    // parchment, darker toward the frame (it curls under it), a few fibres ; ink separators between the days
    pod_wheels_under(dev, cx, cy, rw, wr, th0, as);                     // three paper WHEELS, as the hour's
    // ---- the three WHEELS (yesterday | today | tomorrow), icons upright ; when the day changes the three roll together ----
    if (wxTex) {
        tex_state(dev, wxTex);
        static int lastDi = -1, oldDi = -1; static unsigned rollMs = 0;
        const unsigned nowMs = GetTickCount();
        if (lastDi < 0) lastDi = di;
        if (di != lastDi) { oldDi = lastDi; lastDi = di; rollMs = nowMs; }
        const float k = rollMs ? fminf(1.0f, (float)(nowMs - rollMs) / 240.0f) : 1.0f;
        for (int d = -1; d <= 1; ++d) {
            const bool today = d == 0;
            const float sz = today ? isz : isz * (0.13f / 0.18f);
            const u32 col = today ? 0xFFFFFFFFu : 0xFFD2D2D2u;
            pod_roll_cell(dev, cx, cy, th0 + pod_dir(th0) * (float)d * as, rw + wr * 0.12f, sz, as * rw * 0.48f, wr, k, oldDi < 0 ? -1 : ((oldDi + d + 16) & 7), (di + d + 16) & 7, col, col, th0);
        }
        dSetTex(dev, 0, 0);
        color_state(dev);
    }
    pod_wheels_over(dev, cx, cy, rw, wr, th0, as, Wh, S);               // the cuts between them, the glass
    // ---- the housing's metal OVER the wheel beyond the window's ends (what slipped under the frame is hidden) ----
    pod_lit_band(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 + Wh + 0.02f, th0 + Ph, hb, 0);   // the face's metal over what slipped under
    pod_lit_band(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 - Ph, th0 - Wh - 0.02f, hb, 0);
    pod_glass(dev, cx, cy, rw, wr, th0 - Wh, th0 + Wh, S);              // the glass tube...
    pod_caps(dev, cx, cy, rw, wr, th0 - Wh, th0 + Wh, S);               // ...clamped by its end caps
    color_state(dev);
    (void)PI; (void)capR; (void)frac;
}

// THE POSITION PLATE (the user 2026-11-25 : "straight text in a rounded bulge looks odd, and the text overflows") : a
// HORIZONTAL plate, mounted behind the compass at its upper left (the ring overlaps its inner corner), the same dark metal
// as the day pod, a parchment window sized to the text with a margin -- the grid square in ink, never past it. Drawn
// BEFORE the frame and the map.
static void draw_pos_pod(u32 dev, Font* fo, float cx, float cy, float r, float S, const char* txt) {
    PERF_SCOPE("mm.pospod");
    const PodGeo G = pod_geo(r, S);
    const float tsz = snap(G.isz * 0.92f), tw = fo ? fo->measure(txt, tsz) : 30.0f * S;
    const float ww = snap(tw + 12.0f * S), wh = snap(tsz + 7.0f * S);          // the window : the text and its margin
    const float bz = snap(3.2f * S), hw = ww + 2.0f * bz, hh = wh + 2.0f * bz;   // the plate around it
    const float an = TUNE("mm.pos_angle", -2.3562f), dist = r + TUNE("mm.pos_out", 0.10f) * r;
    const float px = snap(cx + cosf(an) * dist), py = snap(cy + sinf(an) * dist);   // the plate's centre, beyond the ring
    const float x0 = px - hw * 0.5f, y0 = py - hh * 0.5f;
    const u32 hb = dm_toward(g_dm.bot, 0xFF000000u, 0.45f);
    color_state(dev);
    rrect_glow(dev, x0, y0 + 1.5f * S, hw, hh, snap(4.0f * S), 0x80000000u, snap(3.0f * S));   // its shadow
    rrect(dev, x0 - 1.2f * S, y0 - 1.2f * S, hw + 2.4f * S, hh + 2.4f * S, snap(5.0f * S), g_dm.edge, g_dm.edge);
    rrect(dev, x0, y0, hw, hh, snap(4.0f * S), dm_toward(hb, 0xFFFFFFFFu, 0.10f), hb);   // the plate, lit from the top
    grad_quad(dev, x0 + 4.0f * S, y0 + 1.0f, hw - 8.0f * S, 1.0f, MA(0x90, g_dm.glint), MA(0x90, g_dm.glint), MA(0x90, g_dm.glint), MA(0x90, g_dm.glint));   // its lit top edge
    const float wx = x0 + bz, wy = y0 + bz;
    rrect(dev, wx - 0.9f * S, wy - 0.9f * S, ww + 1.8f * S, wh + 1.8f * S, snap(2.4f * S), g_dm.dark, g_dm.lite);   // the window's chamfer
    rrect(dev, wx, wy, ww, wh, snap(2.0f * S), 0xFFDCC28Cu, 0xFFEAD6A6u);           // the parchment, shaded under the lip
    grad_quad(dev, wx + 1.0f, wy, ww - 2.0f, snap(2.0f * S), 0x50603A10u, 0x50603A10u, 0x00603A10u, 0x00603A10u);   // the lip's shadow on it
    if (fo) {
        fo->begin(dev);
        fo->draw_c(dev, snap(px), snap(py + 0.6f), txt, tsz, 0x60FFF4DCu, 0, 0.0f);   // the paper's lit lip under the ink
        fo->draw_c(dev, snap(px), snap(py), txt, tsz, 0xFF3A2410u, 0, 0.0f);
    }
    color_state(dev);
}

void clip_rect_begin(u32 dev, float x, float y, float w, float h);   // ui/config_controls.cpp : the viewport scissor
void clip_rect_end(u32 dev);
// COMBINATION-LOCK WHEELS (the user 2026-11-25 : "mechanical -- papyrus that turns" ; "a wheel for each number" ; "a
// roller only for each element that can move") : a window's text is split into what can CHANGE -- each digit its own
// parchment wheel, a word (New / Full) one wide wheel, the grid column's letter a wheel -- and what never moves -- ':' '-',
// a unit after a number ('d', 'j'), spaces -- ENGRAVED in the metal between the wheels. Each wheel is a cylinder (darker
// toward its top, bottom and sides, a bright crown facing you). When a value changes only the wheels whose text changed
// ROLL one notch (the old up and away, the new in from below, ~0.2 s). The ink stays upright ; each wheel clips its roll.
struct DrumTok { char t[12]; bool wheel; float w; };
static int drum_tokens(Font* fo, const char* txt, float sz, float S, DrumTok* out, int cap) {
    // ONE GAUGE (the user 2026-11-25 : "every wheel the same width, the widest character sets the standard") : every
    // character is its own wheel of one width ; a word is a row of letter wheels padded with blank wheels to the longest
    // word it can show (New -> 4 wheels like Full ; Nouv. / Pleine -> 6), so the mechanism never changes ; a space is a gap
    static const char* WORDS[] = { "New", "Full", "Nouv.", "Pleine" };
    float std = 0.0f;
    if (fo) { static const char* PROBE[] = { "W", "M", "0", "8", "D" }; for (const char* q : PROBE) { const float m2 = fo->measure(q, sz); if (m2 > std) std = m2; } }
    else std = 8.0f * S;
    const float ww = std + 2.6f * S;
    auto isL = [](char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (unsigned char)ch >= 0xC0 || ch == '.'; };
    int n = 0, i = 0;
    if (txt[0] == '' && cap > 0) {                                  //  : the whole text on ONE wheel (the grid square), as wide as "Z-99"
        DrumTok& k = out[n++]; strncpy(k.t, txt + 1, 11); k.t[11] = 0; k.wheel = true;
        k.w = (fo ? fo->measure("Z-99", sz) : 30.0f * S) + 6.0f * S;
        return n;
    }
    while (txt[i] && n < cap) {
        const char ch = txt[i];
        if (ch == ' ') { DrumTok& k = out[n++]; k.t[0] = ' '; k.t[1] = 0; k.wheel = false; k.w = 3.0f * S; ++i; continue; }
        if (isL(ch)) {                                                    // a word : its letters, padded to the longest word
            int j = i; while (txt[j] && isL(txt[j])) ++j;
            const int m = j - i; int pad = m;
            if (m >= 2) {                                                  // padded to the longest word of ITS pair (EN New/Full, FR Nouv./Pleine)
                const bool fr = !strncmp(txt + i, "Nouv", 4) || !strncmp(txt + i, "Pleine", 6);
                for (int q = fr ? 2 : 0; q < (fr ? 4 : 2); ++q) { const int L = (int)strlen(WORDS[q]); if (L > pad) pad = L; }
            }
            for (int q = 0; q < pad && n < cap; ++q) { DrumTok& k = out[n++]; k.t[0] = q < m ? txt[i + q] : ' '; k.t[1] = 0; k.wheel = true; k.w = ww; }
            i = j; continue;
        }
        DrumTok& k = out[n++]; k.t[0] = ch == '' ? ' ' : ch; k.t[1] = 0; k.wheel = true; k.w = ww; ++i;   // a digit, ':' '-',  = a blank wheel : one wheel each
    }
    return n;
}
static float drum_width(Font* fo, const char* txt, float sz, float S) {
    DrumTok tk[16]; const int n = drum_tokens(fo, txt, sz, S, tk, 16);
    float w = 0.0f; for (int i = 0; i < n; ++i) w += tk[i].w; return w;
}
static void drum_text(u32 dev, Font* fo, int slot, float cx, float cy, float hw, float hh, const char* txt, float sz, float S) {
    if (!fo || slot < 0 || slot > 3) return;
    (void)hw;
    DrumTok tk[16]; const int n = drum_tokens(fo, txt, sz, S, tk, 16);
    static char cur[4][16][12] = {}, old[4][16][12] = {}; static unsigned tch[4][16] = {}; static bool seen[4] = {};
    const unsigned now = GetTickCount();
    for (int i = 0; i < 16; ++i) {
        const char* nv = i < n ? tk[i].t : "";
        if (strcmp(nv, cur[slot][i]) != 0) {
            strcpy(old[slot][i], cur[slot][i]); strncpy(cur[slot][i], nv, 11); cur[slot][i][11] = 0;
            tch[slot][i] = seen[slot] ? now : 0;
        }
    }
    seen[slot] = true;                                                    // (the first frame shows the value in place)
    const float tw = drum_width(fo, txt, sz, S), y0 = cy - hh, h = 2.0f * hh, lh = h * 0.95f;
    float x = cx - tw * 0.5f;
    // the APERTURES (the user 2026-11-25 : "the separators part of the foot, the dials cut into the foot with a glass") :
    // each run of wheels sits in its own opening cut in the plinth -- a dark chamfered rim first, the wheels, a glass over
    float gx0[8], gx1[8]; int ng = 0;
    { float xx = x; for (int i = 0; i < n && ng < 8; ++i) {
        if (tk[i].wheel && (i == 0 || !tk[i - 1].wheel)) gx0[ng] = xx;
        xx += tk[i].w;
        if (tk[i].wheel && (i + 1 == n || !tk[i + 1].wheel)) gx1[ng++] = xx; } }
    color_state(dev);
    for (int g = 0; g < ng; ++g) {                                        // the cut : a lit lower lip, a dark upper edge, the hollow
        rrect(dev, gx0[g] - 1.6f * S, y0 - 1.6f * S, gx1[g] - gx0[g] + 3.2f * S, h + 3.2f * S, snap(2.4f * S), g_dm.edge, MA(0xFF, g_dm.lite));
        rrect(dev, gx0[g] - 0.6f * S, y0 - 0.6f * S, gx1[g] - gx0[g] + 1.2f * S, h + 1.2f * S, snap(1.8f * S), 0xFF0A0806u, 0xFF0A0806u);
    }
    for (int i = 0; i < n; ++i) {
        const float ww = tk[i].w;
        if (tk[i].wheel) {
            color_state(dev);
            rrect(dev, x + 0.4f * S, y0, ww - 0.8f * S, h, snap(1.2f * S), 0xFFD6BC86u, 0xFFD6BC86u);   // the wheel's parchment
            rrect(dev, x + ww * 0.14f, y0 + h * 0.24f, ww * 0.72f, h * 0.52f, 0.0f, 0xFFEBD8AAu, 0xFFEBD8AAu);   // its bright crown
            grad_quad(dev, x, y0, ww, h * 0.32f, 0xB03A2410u, 0xB03A2410u, 0x003A2410u, 0x003A2410u);  // turning away : top...
            grad_quad(dev, x, y0 + h * 0.68f, ww, h * 0.32f, 0x003A2410u, 0x003A2410u, 0xB03A2410u, 0xB03A2410u);   // ...bottom
            grad_quad(dev, x, y0, ww * 0.16f, h, 0x603A2410u, 0x003A2410u, 0x603A2410u, 0x003A2410u);   // ...its rounded sides
            grad_quad(dev, x + ww * 0.84f, y0, ww * 0.16f, h, 0x003A2410u, 0x603A2410u, 0x003A2410u, 0x603A2410u);
            const char* ol = old[slot][i];
            const float cw = fo->measure(tk[i].t, sz), ow = ol[0] ? fo->measure(ol, sz) : 0.0f;
            float k = tch[slot][i] ? (float)(now - tch[slot][i]) / 200.0f : 1.0f; if (k > 1.0f) k = 1.0f;
            const float e = 1.0f - (1.0f - k) * (1.0f - k);
            clip_rect_begin(dev, x, y0, ww, h);
            fo->begin(dev);
            if (k < 1.0f && ol[0] && ol[0] != ' ') fo->draw_lc(dev, snap(x + (ww - ow) * 0.5f), snap(cy - e * lh), ol, sz, 0xFF2E1C0Cu, 0, 0.0f);   // the old value rolls away
            if (tk[i].t[0] != ' ') fo->draw_lc(dev, snap(x + (ww - cw) * 0.5f), snap(cy + (1.0f - e) * lh), tk[i].t, sz, 0xFF2E1C0Cu, 0, 0.0f);     // the new one rolls in
            clip_rect_end(dev);
        } else if (tk[i].t[0] != ' ') {                                  // the sign ENGRAVED in the plinth's own metal, between the apertures
            const float cw = fo->measure(tk[i].t, sz);
            fo->begin(dev);
            fo->draw_lc(dev, snap(x + (ww - cw) * 0.5f), snap(cy - 0.8f), tk[i].t, sz, 0xD0000000u, 0, 0.0f);              // the cut's shadow
            fo->draw_lc(dev, snap(x + (ww - cw) * 0.5f), snap(cy), tk[i].t, sz, g_dm.lite | 0xFF000000u, 0, 0.0f);          // INLAID with bright metal : it reads
        }
        x += ww;
        if (false && tk[i].wheel && i + 1 < n && tk[i + 1].wheel && tk[i].t[0] >= '0' && tk[i].t[0] <= '9' && tk[i + 1].t[0] >= '0' && tk[i + 1].t[0] <= '9') {
            color_state(dev);                                              // the cut between two cylinders of one number : a hairline
            rrect(dev, x - 0.5f * S, y0, 1.0f * S, h, 0.0f, 0xC0201408u, 0xC0201408u);
        }
        if (tk[i].wheel && i + 1 < n && tk[i + 1].wheel) {                // the cut between ANY two wheels : a dark hairline, a lit edge
            color_state(dev);
            rrect(dev, x - 0.5f * S, y0, 0.8f * S, h, 0.0f, 0xD0140C04u, 0xD0140C04u);
            rrect(dev, x + 0.3f * S, y0, 0.5f * S, h, 0.0f, 0x40FFF0D0u, 0x40FFF0D0u);
        }
    }
    color_state(dev);
    for (int g = 0; g < ng; ++g) {                                        // the GLASS over each aperture : a sheen on its upper half, a lit edge
        const float gw = gx1[g] - gx0[g] + 1.2f * S;
        grad_quad(dev, gx0[g] - 0.6f * S, y0 - 0.6f * S, gw, h * 0.45f, 0x38FFFFFFu, 0x38FFFFFFu, 0x08FFFFFFu, 0x08FFFFFFu);
        grad_quad(dev, gx0[g] - 0.6f * S, y0 - 0.6f * S, gw, 1.0f, 0x70FFFFFFu, 0x70FFFFFFu, 0x70FFFFFFu, 0x70FFFFFFu);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        const float ra[8] = { gx0[g] + gw * 0.15f, y0 + h * 0.95f, gx0[g] + gw * 0.30f, y0 - 0.6f * S, gx0[g] + gw * 0.38f, y0 - 0.6f * S, gx0[g] + gw * 0.23f, y0 + h * 0.95f };
        fill_poly_aa(dev, ra, 4, 0x18FFFFFFu);                            // a slanted reflection across it
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }
    color_state(dev);
}
// THE WHEELS ON AN ARC (the user 2026-11-25 : "like the others, an orientation that follows the main frame -- the position
// too") : drum_text's wheels laid along the tube's arc -- each wheel an annular cell (pod_wheel_cell) as wide as its gauge,
// the cuts radial, the aperture and the glass following the curve ; the characters stay upright and roll ALONG THE
// RADIUS (outward = the old value leaves away from the centre). th0 the tube's angle, rw its mid radius, wr half its height.
static void drum_arc(u32 dev, Font* fo, int slot, float cx, float cy, float th0, float rw, float wr, const char* txt, float sz, float S) {
    if (!fo || slot < 0 || slot > 3) return;
    DrumTok tk[16]; const int n = drum_tokens(fo, txt, sz, S, tk, 16);
    static char cur[4][16][12] = {}, old[4][16][12] = {}; static unsigned tch[4][16] = {}; static bool seen[4] = {};
    const unsigned now = GetTickCount();
    for (int i = 0; i < 16; ++i) {
        const char* nv = i < n ? tk[i].t : "";
        if (strcmp(nv, cur[slot][i]) != 0) { strcpy(old[slot][i], cur[slot][i]); strncpy(cur[slot][i], nv, 11); cur[slot][i][11] = 0; tch[slot][i] = seen[slot] ? now : 0; }
    }
    seen[slot] = true;
    const float tw = drum_width(fo, txt, sz, S), dir = pod_dir(th0);   // left to right (top to bottom on a side) wherever it sits
    const float span = tw / rw, aS = th0 - dir * span * 0.5f;
    auto ang = [&](float w) { return aS + dir * w / rw; };               // the angle at w along the row
    const float aLo = th0 - span * 0.5f, aHi = th0 + span * 0.5f;
    color_state(dev);
    ring_sector(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, aLo - 1.6f * S / rw, aHi + 1.6f * S / rw, g_dm.edge);   // the cut in the metal
    ring_sector(dev, cx, cy, rw - wr - 1.6f * S, rw - wr - 0.6f * S, aLo, aHi, MA(0xFF, g_dm.lite));                         // its lit lip toward the centre
    ring_sector(dev, cx, cy, rw - wr - 0.6f * S, rw + wr + 0.6f * S, aLo - 0.6f * S / rw, aHi + 0.6f * S / rw, 0xFF0A0806u);  // the hollow
    const float ux0 = cosf(th0), uy0 = sinf(th0);                        // the roll axis (the tube is short : one axis for all)
    float w = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float ww = tk[i].w, a = ang(w + ww * 0.5f);
        if (tk[i].wheel) {
            color_state(dev);
            pod_wheel_cell(dev, cx, cy, rw, wr, a, ww / rw);
            const float px = cx + cosf(a) * rw, py = cy + sinf(a) * rw, lh = 2.0f * wr * 0.95f;
            const char* ol = old[slot][i];
            const float cw = fo->measure(tk[i].t, sz), ow = ol[0] ? fo->measure(ol, sz) : 0.0f;
            float k = tch[slot][i] ? (float)(now - tch[slot][i]) / 200.0f : 1.0f; if (k > 1.0f) k = 1.0f;
            const float ee = 1.0f - (1.0f - k) * (1.0f - k), inn = k < 1.0f ? (1.0f - ee) * lh : 0.0f;
            const bool rolling = k < 1.0f && ol[0] && ol[0] != ' ';
            if (k < 1.0f) clip_rect_begin(dev, px - ww * 0.5f, py - wr, ww, 2.0f * wr);
            fo->begin(dev);
            // the characters FOLLOW THEIR WHEEL (the user 2026-11-25) : each turned to its wheel's angle, upright toward the
            // outside on the top half, toward the centre on the bottom half ; drawn in the wheel's frame, then rotated in
            const float rot = pod_rot(a, th0), ux = cosf(a), uy = sinf(a);
            Font::set_rotation(px, py, rot);
            const float og = sinf(a) > 0.0f ? 1.0f : -1.0f;              // outward, in the figure's own frame (its head points in on the bottom half)
            if (rolling) fo->draw_lc(dev, px - ow * 0.5f, py + og * ee * lh, ol, sz, 0xFF2E1C0Cu, 0, 0.0f);   // the old value rolls OUT (away from the centre)
            if (tk[i].t[0] != ' ') fo->draw_lc(dev, px - cw * 0.5f, py - og * inn, tk[i].t, sz, 0xFF2E1C0Cu, 0, 0.0f);   // the new one rolls in
            Font::clear_rotation();
            (void)ux; (void)uy; (void)ux0; (void)uy0;
            if (k < 1.0f) clip_rect_end(dev);
        }
        w += ww;
        if (tk[i].wheel && i + 1 < n && tk[i + 1].wheel) { color_state(dev); pod_cut(dev, cx, cy, rw, wr, ang(w), S); }
    }
    color_state(dev);
    pod_glass(dev, cx, cy, rw, wr, aLo, aHi, S);                        // the glass tube...
    pod_caps(dev, cx, cy, rw, wr, aLo, aHi, S);                         // ...clamped by its end caps
}
static bool s_tubes = false; static float s_tubeRw = 0.0f;   // the hour / position in their tubes (the plinth off) : drum_arc
static u32 s_moonDialTex = 0;   // the moon dial's face (Minimap::draw sets it each frame ; 0 = draw the inked fallback)
// an UPRIGHT textured rectangle (w x h, its UVs) centred `off` along the radius from a wheel cell's centre, clipped to the
// cell's box (radial half hR, tangential half hT) -- a painted scene seen through its wheel, whatever the wheel's tilt
static void pod_tex_clipped(u32 dev, float ccx, float ccy, float ang, float rc, float off, float w, float h, float hT, float hR,
                            float u0, float u1, float v0, float v1, u32 col, float rot = 0.0f) {
    const float ux = cosf(ang), uy = sinf(ang), tx = -uy, ty = ux, Cx = ccx + ux * rc, Cy = ccy + uy * rc;
    const float px = Cx + ux * off, py = Cy + uy * off, rc_ = cosf(rot), rs_ = sinf(rot);
    const float qx[4] = { -w * 0.5f, w * 0.5f, w * 0.5f, -w * 0.5f }, qy[4] = { -h * 0.5f, -h * 0.5f, h * 0.5f, h * 0.5f };
    const float qu[4] = { u0, u1, u1, u0 }, qv[4] = { v0, v0, v1, v1 };
    float P[16][4];
    for (int i = 0; i < 4; ++i) { P[i][0] = px + qx[i] * rc_ - qy[i] * rs_; P[i][1] = py + qx[i] * rs_ + qy[i] * rc_; P[i][2] = qu[i]; P[i][3] = qv[i]; }
    int n = 4;
    const float pl[4][3] = { { ux, uy, hR }, { -ux, -uy, hR }, { tx, ty, hT }, { -tx, -ty, hT } };
    for (int q = 0; q < 4 && n > 0; ++q) {
        float Q[16][4]; int m = 0;
        for (int i = 0; i < n; ++i) {
            const float* A = P[i]; const float* B = P[(i + 1) % n];
            const float da = (A[0] - Cx) * pl[q][0] + (A[1] - Cy) * pl[q][1] - pl[q][2];
            const float db = (B[0] - Cx) * pl[q][0] + (B[1] - Cy) * pl[q][1] - pl[q][2];
            if (da <= 0.0f && m < 16) { for (int j = 0; j < 4; ++j) Q[m][j] = A[j]; ++m; }
            if ((da <= 0.0f) != (db <= 0.0f) && m < 16) { const float t = da / (da - db); for (int j = 0; j < 4; ++j) Q[m][j] = A[j] + (B[j] - A[j]) * t; ++m; }
        }
        n = m; for (int i = 0; i < n; ++i) for (int j = 0; j < 4; ++j) P[i][j] = Q[i][j];
    }
    if (n < 3) return;
    Vtx v[16];
    for (int i = 0; i < n; ++i) v[i] = { P[i][0] - 0.5f, P[i][1] - 0.5f, 0, 1, col, P[i][2], P[i][3] };
    dDrawUP(dev, D3DPT_TRIANGLEFAN, n - 2, v, sizeof(Vtx));
}
// THE SKY MODULE (the user 2026-11-25 : "rethink the day/night module and the two weather elements, the same technology
// as the rest" ; "won't the weather confuse the user with the day's elements ?") : one tube, read in its slot's order --
// a SKY WINDOW (its colour by the hour : night blue, an orange dawn, a clear noon, dusk ; the inked SUN crosses it by day,
// the MOON by night, rising on one side and setting on the other) then the WEATHER on two wheels of BLACK ENAMEL (not
// parchment : parchment is the calendar, the black is the sky's -- they never read alike) : one element for a single
// weather, the SAME element on both for a double, both bare for none ; they roll together when the weather changes.
static float sky_cell_ratio() { return TUNE("mm.sky_cell", 1.6f); }   // the sky window's width, in wheel widths
static void draw_sky_pod(u32 dev, float cx, float cy, float r, float S, float th0, int hh, int mi, int wid, u32 wxTex, u32 skyTex) {
    PERF_SCOPE("mm.skypod");
    PodGeo G = pod_geo(r, S);
    const float isz = G.isz * 0.92f, rw = G.rw, wr = isz * 0.56f, as = G.as, k = sky_cell_ratio();
    const float Wh = (k + 2.0f) * as * 0.5f, dir = pod_dir(th0);
    const float aStart = th0 - dir * Wh;
    const float aSky = aStart + dir * (k * as * 0.5f), aW[2] = { aStart + dir * (k * as + as * 0.5f), aStart + dir * (k * as + as * 1.5f) };
    color_state(dev);
    ring_sector(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 - Wh - 0.02f, th0 + Wh + 0.02f, g_dm.dark);   // the window's chamfer
    // ---- the SKY : a PAPYRUS WHEEL painted in ink and wash (the user 2026-11-25 : "a day/night cycle, drawn on papyrus,
    // that makes wheels") : the hour's scene -- a wash of sky over the paper (indigo by night, rose and orange at dawn and
    // dusk, a pale blue by day), the sun or the moon inked where the hour puts it, pen stars by night, a line of hills --
    // and the wheel rolls ONE NOTCH AN HOUR along its axis, the hour's scene out, the next one in. Everything is clipped to
    // the wheel's own annular cell (each point clamped to its radii and angles : the roll axis IS the radius).
    const float PI = 3.14159265f, hS = k * as * 0.5f, lo = rw - wr, hi = rw + wr;
    pod_wheel_cell(dev, cx, cy, rw, wr, aSky, k * as);                     // the papyrus wheel itself
    auto scene = [&](int hour, float shift) {
        // a point of the scene (upright frame around the cell's centre, moved by `shift` along the radius) clamped into the cell
        const float ux = cosf(aSky), uy = sinf(aSky);
        const float Cx = cx + ux * (rw + shift), Cy = cy + uy * (rw + shift), hx = hS * rw * 0.80f, hy = wr * 0.80f;
        // the scene FOLLOWS ITS WHEEL'S AXIS (the user 2026-11-25 : the day/night is the exception to the upright icons) :
        // composed upright around the cell's centre, then turned with the wheel (pod_rot1 : head outward on the top half)
        const float rS = pod_rot1(aSky), rcS = cosf(rS), rsS = sinf(rS);
        auto clampP = [&](float x0, float y0, float& ox, float& oy) {
            const float lx = x0 - Cx, ly = y0 - Cy, x = Cx + lx * rcS - ly * rsS, y = Cy + lx * rsS + ly * rcS;
            float dx = x - cx, dy = y - cy, rr = sqrtf(dx * dx + dy * dy), aa = atan2f(dy, dx);
            float da = pod_wrapa(aa - aSky); da = clampf(da, -hS, hS);
            rr = clampf(rr, lo + 0.5f, hi - 0.5f);
            ox = cx + cosf(aSky + da) * rr; oy = cy + sinf(aSky + da) * rr;
        };
        auto blob = [&](float x, float y, float rad, u32 col) {          // an inked or washed disc, clipped to the cell
            float pts[24];
            for (int i = 0; i < 12; ++i) { const float t2 = 6.2831853f * (float)i / 12.0f; clampP(x + cosf(t2) * rad, y + sinf(t2) * rad, pts[2 * i], pts[2 * i + 1]); }
            fill_poly_aa(dev, pts, 12, col);
        };
        auto pen = [&](float x0, float y0, float x1, float y1, float wdt, u32 col) {
            float a0x, a0y, a1x, a1y; clampP(x0, y0, a0x, a0y); clampP(x1, y1, a1x, a1y);
            seg_soft(dev, a0x, a0y, a1x, a1y, wdt, col);
        };
        const float t = (float)hour + 0.5f, elev = -cosf(t / 24.0f * 2.0f * PI);
        float dayK = clampf((elev + 0.1f) / 0.45f, 0.0f, 1.0f); dayK = dayK * dayK * (3.0f - 2.0f * dayK);
        const float duskK = clampf(1.0f - fabsf(elev) / 0.35f, 0.0f, 1.0f);
        // the WASH : a watercolour sky laid on the paper (translucent : the papyrus shows through), stronger toward the top
        u32 wTop = col_lerp(0xE0283C78u, 0x9C6EA6DCu, dayK), wBot = col_lerp(0xC0405890u, 0x60A8CCE8u, dayK);   // the night wash deeper : it must read as night
        wTop = col_lerp(wTop, 0xA8604880u, duskK * 0.5f); wBot = col_lerp(wBot, 0xB0E89858u, duskK * 0.8f);
        const bool topHalf = sinf(aSky) <= 0.0f;                          // the sky's top : the screen's top
        const float b0 = clampf(lo + shift, lo, hi), b1 = clampf(hi + shift, lo, hi);
        if (b1 - b0 > 0.3f) {
            auto at = [&](float rr) { const float q2 = (rr - (lo + shift)) / (hi - lo); return col_lerp(topHalf ? wBot : wTop, topHalf ? wTop : wBot, clampf(q2, 0.0f, 1.0f)); };
            sector_grad(dev, cx, cy, b0, b1, aSky - hS, aSky + hS, at(b0), at(b1), at(b0), at(b1));
        }
        // pen STARS by night : small inked crosses, pale ink on the indigo
        if (dayK < 0.6f) {
            static const float SX[6] = { -0.72f, -0.30f, 0.10f, 0.48f, 0.80f, -0.05f }, SY[6] = { -0.55f, -0.15f, -0.62f, -0.30f, -0.58f, 0.18f };
            const u32 sc = MA((u32)(210.0f * (1.0f - dayK / 0.6f)), 0xF4E6BCu);
            for (int i = 0; i < 6; ++i) {
                const float x = Cx + SX[i] * hx, y = Cy + SY[i] * hy, l = 1.6f * S;
                pen(x - l, y, x + l, y, 0.7f * S, sc); pen(x, y - l, x, y + l, 0.7f * S, sc);
            }
        }
        // the SUN by day, the MOON by night : inked, rising on the left, setting on the right
        for (int body = 0; body < 2; ++body) {
            const float tb = body == 0 ? t : fmodf(t + 12.0f, 24.0f), ph = (tb - 6.0f) / 12.0f;
            if (ph <= 0.0f || ph >= 1.0f) continue;
            const float xs = -cosf(ph * PI), ys = sinf(ph * PI);
            const float bx = Cx + xs * hx * 0.80f, by = Cy + hy * 0.55f - ys * hy * 1.15f, br = wr * 0.30f;
            if (body == 0) {
                for (int i = 0; i < 8; ++i) {                              // its rays, pen strokes
                    const float ra = 6.2831853f * (float)i / 8.0f + 0.2f;
                    pen(bx + cosf(ra) * br * 1.25f, by + sinf(ra) * br * 1.25f, bx + cosf(ra) * br * 1.75f, by + sinf(ra) * br * 1.75f, 0.8f * S, 0xC05A3410u);
                }
                blob(bx, by, br + 0.7f * S, 0xD05A3410u);                  // the pen outline
                blob(bx, by, br, 0xFFF0C658u);                             // the gold wash
            } else {
                blob(bx, by, br + 0.7f * S, 0xC03C2614u);
                blob(bx, by, br, 0xFFEEE4C2u);
                blob(bx + br * 0.45f, by - br * 0.22f, br * 0.82f, wTop | 0xFF000000u);   // the crescent : the night bites it
            }
        }
        // the HILLS : a pen line along the foot, an ink wash under it
        {
            const float gy = Cy + hy * 0.95f;
            const float hx3[5] = { -1.3f, -0.55f, 0.05f, 0.65f, 1.3f }, hy3[5] = { 0.02f, -0.20f, -0.06f, -0.26f, -0.04f };
            for (int i = 0; i < 4; ++i)
                pen(Cx + hx3[i] * hx, gy + hy3[i] * hy, Cx + hx3[i + 1] * hx, gy + hy3[i + 1] * hy, 1.0f * S, 0xC03C2614u);
            float pts[12];
            for (int i = 0; i < 5; ++i) clampP(Cx + hx3[i] * hx, gy + hy3[i] * hy, pts[2 * i], pts[2 * i + 1]);
            clampP(Cx + 1.3f * hx, gy + hy * 0.8f, pts[8], pts[9]); clampP(Cx - 1.3f * hx, gy + hy * 0.8f, pts[10], pts[11]);
            float hill[12] = { pts[0], pts[1], pts[2], pts[3], pts[4], pts[5], pts[6], pts[7], pts[8], pts[9], pts[10], pts[11] };
            (void)hill;
            float q[8] = { pts[0], pts[1], pts[8] - (pts[8] - pts[0]), pts[11], pts[8], pts[9], pts[8], pts[9] };
            (void)q;
            for (int i = 0; i < 4; ++i) {                                  // the wash under the line : one quad per span
                float pa[8];
                clampP(Cx + hx3[i] * hx, gy + hy3[i] * hy, pa[0], pa[1]);
                clampP(Cx + hx3[i + 1] * hx, gy + hy3[i + 1] * hy, pa[2], pa[3]);
                clampP(Cx + hx3[i + 1] * hx, gy + hy * 0.9f, pa[4], pa[5]);
                clampP(Cx + hx3[i] * hx, gy + hy * 0.9f, pa[6], pa[7]);
                fill_poly_aa(dev, pa, 4, MA(0x70, 0x5A4A28u));
            }
        }
    };
    if (skyTex) {   // THE PAINTED SCENES (assets/sky_frames.raw, scripts/gen_sky_frames.py : twelve hand-inked scenes on papyrus,
                    // one every two hours) : the wheel shows scene hour / 2 and rolls one notch every two Vana'diel hours
        static int lastF = -1, oldF = -1; static unsigned fMs = 0;
        const int fr = ((hh < 0 ? 0 : hh) / 2) % 12;
        const unsigned nowF = GetTickCount();
        if (lastF < 0) lastF = fr;
        if (fr != lastF) { oldF = lastF; lastF = fr; fMs = nowF; }
        const float kF = fMs ? fminf(1.0f, (float)(nowF - fMs) / 320.0f) : 1.0f;
        const float cw = k * as * rw, qh = 2.0f * wr * 1.04f, qw = qh * (256.0f / 170.0f), hT = cw * 0.5f, hR = wr;   // fitted to the wheel's HEIGHT : the whole scene, the horizon and the sun included
        auto uv = [&](int f2, float& u0, float& u1, float& v0, float& v1) {
            u0 = (float)(f2 % 4) * 256.0f / 1024.0f; u1 = u0 + 256.0f / 1024.0f; v0 = (float)(f2 / 4) * 170.0f / 512.0f; v1 = v0 + 170.0f / 512.0f;
        };
        tex_state(dev, skyTex);
        float u0, u1, v0, v1;
        const float rotS = pod_rot1(aSky);                               // the SKY follows its wheel's axis (the user 2026-11-25 : the exception to the upright icons)
        if (kF >= 1.0f || oldF < 0) { uv(fr, u0, u1, v0, v1); pod_tex_clipped(dev, cx, cy, aSky, rw, 0.0f, qw, qh, hT, hR, u0, u1, v0, v1, 0xFFFFFFFFu, rotS); }
        else {
            const float e = 1.0f - (1.0f - kF) * (1.0f - kF), H2 = 2.0f * wr;
            uv(oldF, u0, u1, v0, v1); pod_tex_clipped(dev, cx, cy, aSky, rw, e * H2, qw, qh, hT, hR, u0, u1, v0, v1, 0xFFFFFFFFu, rotS);
            uv(fr, u0, u1, v0, v1);   pod_tex_clipped(dev, cx, cy, aSky, rw, -(1.0f - e) * H2, qw, qh, hT, hR, u0, u1, v0, v1, 0xFFFFFFFFu, rotS);
        }
        dSetTex(dev, 0, 0);
        color_state(dev);
        pod_wheel_shade(dev, cx, cy, rw, wr, aSky, k * as);               // the wheel turning away, over the painting
        (void)mi;
    } else
    {   // the hour's notch : the scene rolls when the hour changes (outward : the old one leaves away from the centre)
        static int lastH = -1, oldH = -1; static unsigned hMs = 0;
        const unsigned nowH = GetTickCount();
        if (lastH < 0) lastH = hh;
        if (hh != lastH) { oldH = lastH; lastH = hh; hMs = nowH; }
        const float kH = hMs ? fminf(1.0f, (float)(nowH - hMs) / 320.0f) : 1.0f;
        color_state(dev);
        if (kH >= 1.0f || oldH < 0) scene(hh, 0.0f);
        else {
            const float e = 1.0f - (1.0f - kH) * (1.0f - kH), H2 = 2.0f * wr;
            scene(oldH, e * H2);
            scene(hh, -(1.0f - e) * H2);
        }
        (void)mi;
    }
    // ---- the WEATHER : two papyrus wheels (one element for a single weather, the same twice for a double) ----
    static const int WCELL[8] = { 0, 2, 1, 3, 4, 5, 6, 7 };              // weather pair -> the element's cell (the icons' order)
    const int cellNow = (wid >= 4 && wid <= 19) ? WCELL[(wid - 4) / 2] : -1, nNow = cellNow < 0 ? 0 : ((wid & 1) ? 2 : 1);
    const int want[2] = { nNow >= 1 ? cellNow : -1, nNow >= 2 ? cellNow : -1 };
    static int cur[2] = { -2, -2 }, old[2] = { -1, -1 }; static unsigned rollMs = 0;
    const unsigned nowMs = GetTickCount();
    if (cur[0] == -2) { cur[0] = want[0]; cur[1] = want[1]; }
    if (want[0] != cur[0] || want[1] != cur[1]) { old[0] = cur[0]; old[1] = cur[1]; cur[0] = want[0]; cur[1] = want[1]; rollMs = nowMs; }
    const float kR = rollMs ? fminf(1.0f, (float)(nowMs - rollMs) / 240.0f) : 1.0f;
    color_state(dev);
    for (int q = 0; q < 2; ++q) pod_wheel_cell(dev, cx, cy, rw, wr, aW[q], as);   // PAPYRUS, as every wheel (the user 2026-11-25 : "stay coherent with the technology")
    if (wxTex) {
        tex_state(dev, wxTex);
        for (int q = 0; q < 2; ++q)
            pod_roll_cell(dev, cx, cy, aW[q], rw, isz * 0.86f, as * rw * 0.48f, wr, kR, rollMs ? (old[q] < 0 ? -1 : old[q]) : -1, cur[q], 0xFFFFFFFFu, 0xFFFFFFFFu, th0);
        dSetTex(dev, 0, 0);
    }
    color_state(dev);
    for (int q = 0; q < 2; ++q) {                                         // the cuts : sky | weather | weather -- a dark cut, a lit brass edge
        const float sa = aStart + dir * (k * as + as * (float)q);
        pod_cut(dev, cx, cy, rw, wr, sa, S);
        const float tx = -sinf(sa) * dir, ty = cosf(sa) * dir;
        seg_soft(dev, cx + cosf(sa) * (rw - wr) + tx * 0.9f * S, cy + sinf(sa) * (rw - wr) + ty * 0.9f * S,
                      cx + cosf(sa) * (rw + wr) + tx * 0.9f * S, cy + sinf(sa) * (rw + wr) + ty * 0.9f * S, 0.7f * S, MA(0x90, g_dm.lite));
    }
    pod_glass(dev, cx, cy, rw, wr, th0 - Wh, th0 + Wh, S);
    pod_caps(dev, cx, cy, rw, wr, th0 - Wh, th0 + Wh, S);
    color_state(dev);
}
static float sky_pod_ph(float r, float S) {                               // its half span, for the collar
    const PodGeo G = pod_geo(r, S);
    return (sky_cell_ratio() + 2.0f) * G.as * 0.5f + 2.6f * S / G.rm + 0.04f;
}

// THE MOON POD (the user 2026-11-25 : "the same principle as the elements, yesterday / today / tomorrow" -- and the
// moons "must stay the right way up") : the day pod's twin, at the compass's upper left. A housing on the ring, a window
// on a PAPER band, three phases INKED upright on it (assets/moon_phases.raw, scripts/gen_moon_phases.py : 0 new ..
// 4 full .. 7 waning crescent) : the present one whole and bright in the middle, the one before and the one after
// dimmer either side ; when the phase changes the band snaps one notch (the click). Drawn before the frame : the
// compass covers its root.
static void draw_moon_pod(u32 dev, float cx, float cy, float r, float S, int pct, bool waning, int hh, u32 tex) {
    PERF_SCOPE("mm.moonpod");
    PodGeo G = pod_geo(r, S);
    G.th0 = s_moonTh;                                                    // the moon : its slot (Minimap > Clock, the user 2026-11-25)
    const float dir = pod_dir(G.th0);                                    // read left to right (top to bottom on a side) wherever it sits
    const float th0 = G.th0, Ri = G.Ri, Ro = G.Ro, rm = G.rm, isz = G.isz * TUNE("mm.mpod_icon", 1.10f), rw = G.rw, wr = isz * 0.56f, as = G.as, Wh = G.Wh, Ph = G.Ph;
    auto at = [&](float ang, float rad, float& x, float& y) { x = cx + cosf(ang) * rad; y = cy + sinf(ang) * rad; };
    const u32 hb = dm_toward(g_dm.bot, 0xFF000000u, TUNE("mm.pod_dark", 0.25f));
    color_state(dev);
    (void)Ri; (void)Ro;                                                  // (the housing : the collar, drawn once for all the modules)
    (void)rm;
    ring_sector(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 - Wh - 0.02f, th0 + Wh + 0.02f, g_dm.dark);     // the window's chamfer
    pod_wheels_under(dev, cx, cy, rw, wr, th0, as);                     // three paper WHEELS, as the hour's
    // the three WHEELS : the last NEW or FULL before | the present moon, inked to its exact phase | the next NEW or FULL ;
    // the side wheels roll together when the cycle turns (at the full and at the new)
    {
        const int prevC = waning ? 4 : 0, nextC = waning ? 0 : 4;        // waxing : from a new toward a full ; waning : the reverse
        static int lastW = -1; static unsigned rollMs = 0;
        const unsigned nowMs = GetTickCount();
        const int w = waning ? 1 : 0;
        if (lastW < 0) lastW = w;
        const bool rolled = w != lastW;
        if (rolled) { lastW = w; rollMs = nowMs; }
        const float k = rollMs ? fminf(1.0f, (float)(nowMs - rollMs) / 240.0f) : 1.0f;
        const float szS = isz * (0.13f / 0.18f);
        if (tex) {
            tex_state(dev, tex);
            for (int d = -1; d <= 1; d += 2) {
                const int nc = d < 0 ? prevC : nextC, oc = d < 0 ? nextC : prevC;   // a flip swaps the two
                pod_roll_cell(dev, cx, cy, th0 + dir * (float)d * as, rw, szS, as * rw * 0.48f, wr, k, rollMs ? oc : -1, nc, 0xB4FFFFFFu, 0xB4FFFFFFu, th0);   // ink, not solid
            }
            dSetTex(dev, 0, 0);
        }
        // the PRESENT moon : its wheel turns when the GAME changes it -- once a Vana'diel day, at midnight (the user
        // 2026-11-25 : "per day, more coherent") ; the old moon rolls out along the wheel's axis (outward), the new one in,
        // on a drum (each point at Rcyl sin(phi + d / Rcyl), gone past the window's edge)
        {
            (void)hh;
            const float fNow = (float)pct / 100.0f;
            const int key = pct * 2 + (waning ? 1 : 0);
            static int lastKey = -1; static float fOld = 0.0f, fCur = 0.0f; static bool wOld = false, wCur = false; static unsigned mRoll = 0;
            if (lastKey < 0) { lastKey = key; fCur = fNow; wCur = waning; }
            if (key != lastKey) { lastKey = key; fOld = fCur; wOld = wCur; fCur = fNow; wCur = waning; mRoll = nowMs; }
            const float km = mRoll ? fminf(1.0f, (float)(nowMs - mRoll) / 240.0f) : 1.0f;
            float px, py; at(th0, rw, px, py);
            const float ux = cosf(th0), uy = sinf(th0), Rm = isz * 0.44f, Rc = wr;
            s_inkRotC = 1.0f; s_inkRotS = 0.0f;                           // upright to the player's eye, as the icons
            if (km >= 1.0f) draw_ink_moon(dev, snap(px), snap(py), Rm, fCur, wCur, S);
            else {
                const float e = 1.0f - (1.0f - km) * (1.0f - km), PH = 1.3f, Rcyl = 2.0f * Rm;
                const float phO = e * PH, phN = -(1.0f - e) * PH;        // the old turns away outward, the new comes up from the centre's side
                auto al = [&](float ph) { const float q = fabsf(ph) / PH; return (u32)(255.0f * (1.0f - q * q)); };
                draw_ink_moon(dev, snap(px), snap(py), Rm, fOld, wOld, S, ux, uy, phO, Rcyl, Rc, al(phO));
                draw_ink_moon(dev, snap(px), snap(py), Rm, fCur, wCur, S, ux, uy, phN, Rcyl, Rc, al(phN));
            }
        }
        color_state(dev);
        s_inkRotC = 1.0f; s_inkRotS = 0.0f;
        pod_wheels_over(dev, cx, cy, rw, wr, th0, as, Wh, S);           // the cuts between them, the glass
    }
    // the housing's metal over the band beyond the window's ends (what slipped under is hidden)
    pod_lit_band(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 + Wh + 0.02f, th0 + Ph, hb, 0);   // the face's metal over what slipped under
    pod_lit_band(dev, cx, cy, rw - wr - 1.6f * S, rw + wr + 1.6f * S, th0 - Ph, th0 - Wh - 0.02f, hb, 0);
    pod_glass(dev, cx, cy, rw, wr, th0 - Wh, th0 + Wh, S);              // the glass tube...
    pod_caps(dev, cx, cy, rw, wr, th0 - Wh, th0 + Wh, S);               // ...clamped by its end caps
    color_state(dev);
}


static void draw_dial_instrument(u32 dev, const Frame& f, const UiConfig& c, const GameState& g, const VanaClock& v, u32 elemTex, u32 wxTex,
                                 float mapX, float mapY, float cx, float cy, float rc, float r, float S, float zoomA, bool sq, int pass) {
    // pass 0 : the MODULES -- sky, weather, moon, position, the band -- drawn BEFORE the map and the ring : they are mounted
    // BEHIND the compass, on a lower plane, and peek out from under its case (the user 2026-11-25 : "everything that comes
    // out of the compass comes from behind ; the main dial is in front"). pass 1 : the compass face (cardinals, needle, zoom).
    (void)g;
    s_lampX = cx; s_lampY = cy - r * 0.15f; s_lampH = r;                 // THE LAMP : above the dial's centre, a little north
    Font* tf = band_font(f, MM_TIME); Font* fo = f.font;
    const int di = (v.dayIdx < 0 || v.dayIdx > 7) ? 2 : v.dayIdx;
    const float PI = 3.14159265f;
    if (pass == 0) {
    // -- the four COMPLICATIONS, one per corner of the dial's square (the only room left by the round dial ; nothing goes
    //    past that square). A subdial of radius sr fits a corner while  0.707 (r + sr + gap) + sr <= r  ->  sr ~ 0.165 r.
    //    TL : the map square ; TR : the 24 h watch subdial ; BL : the moon + its plate ; BR : the day + its plate (the
    //    time, tomorrow) --
    {
        const float dk = 0.7071f, sr = snap(r * TUNE("mm.dial_sub_k", 0.165f));
        const float rr = r + sr + TUNE("mm.dial_sub_gap", 1.5f) * S;
        const float off = sq ? r - (r - rc) * 0.5f : dk * rr;      // the square : centred on the frame's corners
        auto bezel = [&](float x, float y, u32 face, bool onBand = false) {                         // the brass case + an enamel face
            color_state(dev);
            if (!onBand) {                                                                         // on the band, its shadow and dark rim were drawn
                rrect_glow(dev, x - sr - 2.0f * S, y - sr - 1.0f * S, 2.0f * sr + 4.0f * S, 2.0f * sr + 4.0f * S, sr + 2.0f * S, 0x80000000u, snap(3.0f * S));   // UNDER it, with the band's : one outline
                color_state(dev);
                disc(dev, x, y, sr + 2.4f * S, g_dm.edge);
            }
            disc(dev, x, y, sr + 1.6f * S, g_dm.body);
            ring_sector(dev, x, y, sr + 0.3f * S, sr + 1.6f * S, PI, 1.5f * PI, MA(0xA0, g_dm.glint));
            ring_sector(dev, x, y, sr + 0.3f * S, sr + 1.6f * S, 0.0f, 0.5f * PI, MA(0x90, g_dm.shade));
            disc(dev, x, y, sr + 0.3f * S, g_dm.dark);
            disc(dev, x, y, sr, face);
            disc(dev, x, y - sr * 0.12f, sr * 0.86f, 0x14FFFFFFu);                                // the light falls from the top
        };
        auto engrave = [&](float x, float y, const char* t, float sz, u32 col) {                    // gilt relief, centred
            if (!tf) return;
            tf->begin(dev);
            tf->draw_c(dev, snap(x), snap(y + 1.0f), t, sz, 0xFF000000u, 0, 0.0f);
            tf->draw_c(dev, snap(x), snap(y), t, sz, col, 0, 0.0f);
        };
        PERF_BEGIN("mm.tl");
        // ---- TL : the map square (the POSITION POD replaces it in the modules style, round) ----
        if (c.mmGrid && !(c.mmDayDial == 2 && !sq)) {
            const float x = cx - off, y = cy - off;
            const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
            if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) {
                bezel(x, y, 0xFF17130Cu);
                color_state(dev);
                const Lamp Lt = lamp_for(x, y);
                static SphCache tlCache; const MeshKey tlKey = { { x, y, sr, Lt.x, Lt.y } };
                sphere_mesh_c(dev, tlCache, tlKey, x, y, sr, [Lt](float u, float v, float z) -> u32 {      // a DOMED face, lit from the lamp
                    float dif = Lt.x * u + Lt.y * v + Lt.z * z; if (dif < 0.0f) dif = 0.0f;
                    return rgb_scale(0xFF2A2214u, (0.55f + 0.75f * dif) * (0.60f + 0.40f * z));
                });
                ring_sector(dev, x, y, sr - 0.8f, sr + 0.6f, 0.0f, 6.2832f, 0x90060A10u);
                for (int k = 1; k <= 2; ++k) { const float gr2 = sr * (0.45f + 0.25f * (float)k); color_state(dev); rrect_stroke(dev, x - gr2, y - gr2, 2.0f * gr2, 2.0f * gr2, gr2, 0x16F4E4B8u, 1.0f); }
                char gb[12]; sprintf(gb, "%c-%d", 'A' + gc, gr + 1);
                float gsz = snap(sr * 0.62f); if (tf) while (gsz > sr * 0.4f && tf->measure(gb, gsz) > sr * 1.6f) gsz -= 1.0f;
                engrave(x, y, gb, gsz, 0xFFF0D898u);
                color_state(dev);
                glass_dome(dev, x, y, sr, 0xFFF0D898u);                                 // the glass over the engraving
            }
        }
        const float pSz = snap(TUNE("mm.dial_cap", 12.5f) * S);                                  // the engraved text
        auto plateText = [&](float x, float ty, const char* t, u32 col, int iconCell) {
            if (!tf) return;
            const float isz = iconCell >= 0 ? snap(pSz * 1.15f) : 0.0f, gp = 3.0f * S;
            const float tw = tf->measure(t, pSz), W = tw + (iconCell >= 0 ? isz + gp : 0.0f), tx = x - W * 0.5f;
            tf->begin(dev);
            tf->draw_lc(dev, snap(tx), snap(ty + 1.0f), t, pSz, 0xFF000000u, 0, 0.0f);
            tf->draw_lc(dev, snap(tx), snap(ty), t, pSz, col, 0, 0.0f);
            if (iconCell >= 0 && elemTex) {
                tex_state(dev, elemTex);
                tquad(dev, snap(tx + tw + gp), snap(ty - isz * 0.5f), isz, isz, iconCell / 8.0f, (iconCell + 1) / 8.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
                dSetTex(dev, 0, 0);
            }
            color_state(dev);
        };
        PERF_END("mm.tl");
        PERF_BEGIN("mm.sky");
        // ---- TR : THE SKY SPHERE + THE WEATHER ORBS (the user 2026-11-25 : "no weather in the sphere, only the sun and the moon,
        //      well recognisable, a blue sky by day and a night blue for the moon ; below, two small spheres that may
        //      overflow the main one on the sides, the elements set in them") : the sky's colour follows the hour (night
        //      blue, an orange dawn, a clear noon, dusk) ; the SUN (a gold disc with its rays) and a crescent MOON ride an arc,
        //      setting below its lower edge ; a glass dome over it. The weather (*(g+0x40)+0x04, verified live) : its
        //      element in small glass ORBS at the bottom, overlapping the rim -- one for a single weather, two for a double ----
        if (c.mmClock && (c.mmClkTime || c.mmClkDay) && !(s_skyPod && !sq && c.mmDayDial == 2)) {   // (the SKY MODULE replaces it)
            const float x = cx + off, y = cy - off;
            bezel(x, y, 0xFF0E1830u);
            const float t = (float)v.hh + (float)v.mm / 60.0f;
            const float elev = -cosf(t / 24.0f * 2.0f * PI);                                         // the sun's height : -1 midnight .. +1 noon
            float dayK = (elev + 0.1f) / 0.45f; dayK = clampf(dayK, 0.0f, 1.0f); dayK = dayK * dayK * (3.0f - 2.0f * dayK);
            float duskK = 1.0f - fabsf(elev) / 0.35f; duskK = clampf(duskK, 0.0f, 1.0f);
            auto mix = [](u32 a2, u32 b2, float k) -> u32 {
                const float q = 1.0f - k;
                return 0xFF000000u | ((u32)(((a2 >> 16) & 0xFF) * q + ((b2 >> 16) & 0xFF) * k) << 16)
                                   | ((u32)(((a2 >> 8) & 0xFF) * q + ((b2 >> 8) & 0xFF) * k) << 8) | (u32)((a2 & 0xFF) * q + (b2 & 0xFF) * k);
            };
            u32 skyTop = mix(0xFF0A1640u, 0xFF2E6ED8u, dayK), skyBot = mix(0xFF1C3470u, 0xFF9CCCF4u, dayK);   // night BLUE (not black) .. day blue
            skyTop = mix(skyTop, 0xFF3A3C7Cu, duskK * 0.5f); skyBot = mix(skyBot, 0xFFF0A050u, duskK * 0.85f);
            color_state(dev);
            // THE SKY, THE SUN AND THE MOON, CLIPPED TO THE DIAL (the user 2026-11-25 : "they must never go past the piece ; the
            // aura that goes past must be cut at the dial's edge") : no stencil here (minimap.cpp's note), so the clip is
            // GEOMETRIC -- one polar mesh covering exactly the dial's disc, each vertex's colour computed : the sky's
            // gradient, then the sun (its glow, 8 turning rays, a rimmed gold disc, a highlight) and a crescent moon with
            // its halo. Whatever lies past the rim is simply never drawn.
            const float hz = y + sr * 0.35f, AR = sr * 0.78f;
            float sx = 0, sy = 0, svis = 0, mx = 0, my = 0, mvis = 0;
            for (int body = 0; body < 2; ++body) {
                const float tt = body == 0 ? t : fmodf(t + 12.0f, 24.0f);                             // the moon is opposite the sun
                const float ph = (tt - 6.0f) / 12.0f * PI;                                            // 0 at its rise, pi at its set
                const float bx = x - cosf(ph) * AR, by = hz - sinf(ph) * AR;
                const float up = body == 0 ? elev : -elev;                                           // above the horizon BY THE HOUR (the user :
                const float vis = clampf((up + 0.04f) / 0.10f, 0.0f, 1.0f);                           // the sun stayed low right after nightfall)
                if (body == 0) { sx = bx; sy = by; svis = vis; } else { mx = bx; my = by; mvis = vis; }
            }
            const float tq = 0.0f;                                                                   // the rays stand still : the sky is rebuilt once per Vana'diel minute (perf : turning them rebuilt it 6 x a second)
            const float sR = sr * 0.20f, mR = sr * 0.30f, rot = tq * 0.25f, px1 = 1.0f;              // px1 : one pixel, the soft edge
            auto smooth = [](float e0, float e1, float v2) { float k = (v2 - e0) / (e1 - e0); k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k); return k * k * (3.0f - 2.0f * k); };
            auto over = [](float* c3, u32 col, float a2) { if (a2 <= 0.0f) return; if (a2 > 1.0f) a2 = 1.0f;
                for (int q = 0; q < 3; ++q) c3[q] = c3[q] * (1.0f - a2) + (float)((col >> (q * 8)) & 0xFF) * a2; };
            auto skyAt = [&](float px, float py) -> u32 {
                const float kk = clampf((py - (y - sr)) / (2.0f * sr), 0.0f, 1.0f);
                const u32 sk = mix(skyTop, skyBot, kk);
                float c3[3] = { (float)(sk & 0xFF), (float)((sk >> 8) & 0xFF), (float)((sk >> 16) & 0xFF) };
                if (svis > 0.0f) {                                                                    // the SUN
                    const float dx = px - sx, dy = py - sy, d = sqrtf(dx * dx + dy * dy);
                    over(c3, 0x00FFE8A0u, svis * 0.55f * expf(-(d - sR) * (d - sR) / (sR * sR * 1.6f)) * (d > sR ? 1.0f : 1.0f));   // its glow
                    if (d > sR * 1.15f && d < sR * 1.95f) {                                           // 8 rays, turning
                        const float an = atan2f(dy, dx) - rot, w = cosf(an * 4.0f);
                        const float ray = smooth(0.82f, 0.90f, w * w) * (1.0f - smooth(sR * 1.6f, sR * 1.95f, d)) * smooth(sR * 1.15f, sR * 1.3f, d);
                        over(c3, 0x00FFD060u, svis * ray);
                    }
                    over(c3, 0x00C88A20u, svis * (1.0f - smooth(sR + 0.7f - px1, sR + 0.7f, d)));      // the rim
                    over(c3, 0x00FFD86Au, svis * (1.0f - smooth(sR - px1, sR, d)));                  // the disc
                    const float hx = px - (sx - sR * 0.3f), hy = py - (sy - sR * 0.3f);
                    over(c3, 0x00FFFFFFu, svis * 0.55f * (1.0f - smooth(sR * 0.30f, sR * 0.40f, sqrtf(hx * hx + hy * hy))));
                }
                if (mvis > 0.0f) {                                                                    // the MOON : a halo, then a crescent
                    const float dx = px - mx, dy = py - my, d = sqrtf(dx * dx + dy * dy);
                    over(c3, 0x00C8D8FFu, mvis * 0.30f * expf(-(d - mR) * (d - mR) / (mR * mR * 0.8f)));
                    const float ex = px - (mx + mR * 0.48f), ey = py - (my - mR * 0.18f), e = sqrtf(ex * ex + ey * ey);
                    const float lit = (1.0f - smooth(mR - px1, mR, d)) * smooth(mR * 0.88f - px1, mR * 0.88f, e);   // the disc, bitten by the night
                    over(c3, 0x00F4ECD4u, mvis * lit);
                }
                const float below = smooth(hz - 1.0f, hz + 1.0f, py);                                // the HORIZON : the land in front of the sun and
                over(c3, mix(skyBot, 0xFF0A0E16u, 0.80f), below);                                     // the moon -- they set BEHIND it
                return 0xFF000000u | ((u32)c3[2] << 16) | ((u32)c3[1] << 8) | (u32)c3[0];
            };
            {
                const int NR = 22, NSg = 96;                                    // fine : the sun, the moon and the rim stay smooth (cached anyway)
                static VtxC skyV[NR * 2 * (NSg + 1)]; static MeshKey skyKey = {}; static bool skyOk = false;
                const MeshKey key = { { x, y, sr, (float)v.hh * 60.0f + (float)v.mm, tq, (float)skyTop, (float)skyBot } };
                if (mesh_key_changed(skyKey, key, skyOk)) {
                    for (int i = 0; i < NR; ++i) {
                        const float r0 = sr * (float)i / NR, r1 = sr * (float)(i + 1) / NR;
                        VtxC* strip = skyV + i * 2 * (NSg + 1);
                        for (int j = 0; j <= NSg; ++j) {
                            const float an = (float)j / NSg * 6.2831853f, ca = cosf(an), sa = sinf(an);
                            for (int q = 0; q < 2; ++q) {
                                const float rr = q == 0 ? r0 : r1, px = x + ca * rr, py = y + sa * rr;
                                strip[2 * j + q] = { px - 0.5f, py - 0.5f, 0, 1, skyAt(px, py) };
                            }
                        }
                    }
                }
                for (int i = 0; i < NR; ++i) dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * NSg, skyV + i * 2 * (NSg + 1), sizeof(VtxC));
            }
            static const float STX[9] = { -0.55f, -0.2f, 0.15f, 0.5f, -0.38f, 0.32f, 0.0f, -0.68f, 0.66f }, STY[9] = { -0.62f, -0.78f, -0.55f, -0.70f, -0.30f, -0.25f, -0.88f, -0.05f, -0.12f };
            const float starA = (1.0f - dayK) * (1.0f - duskK * 0.6f);
            for (int k = 0; k < 9; ++k) {                                                             // fine stars, never on the moon
                const float stx = x + STX[k] * sr, sty = y + STY[k] * sr, dmx = stx - mx, dmy = sty - my;
                if (starA > 0.02f && dmx * dmx + dmy * dmy > mR * mR * 1.4f)
                    disc(dev, snap(stx), snap(sty), (k % 3 == 0) ? 0.45f : 0.3f, ((u32)(starA * (k % 3 == 0 ? 210.0f : 150.0f)) << 24) | 0x00FFF6DCu);
            }
            color_state(dev);
            ring_sector(dev, x, y, sr - 0.8f, sr + 0.4f, 0.0f, 2.0f * PI, g_dm.dark);              // a clean rim
            glass_dome(dev, x, y, sr, 0xFFB8D4FFu, 0.8f);
            color_state(dev);
        }
        PERF_END("mm.sky");
        PERF_BEGIN("mm.band");
        // ---- the BAND : ONE brass bar under the ring, from the moon subdial to the day subdial (both sit on its ends ; it
        //      touches the ring at the S). Two compartments, each under its subdial and lined with ITS colour -- night blue for
        //      the moon, the element's colour for the day -- a gilt cabochon between them : which line is whose reads at once ----
        const bool bandMoon = c.mmClock && c.mmClkMoon, bandDay = c.mmClock && (c.mmClkTime || c.mmClkDay);
        const float bandH = snap(pSz + 12.0f * S), by0 = snap(sq ? cy + off + sr * 0.95f : cy + r - 1.0f * S), bmid = by0 + bandH * 0.5f + 0.5f * S;   // the square : under its corner subdials, as on the round
        const float bx0 = snap(sq ? cx - r : cx - off - sr * 0.8f - TUNE("mm.dial_fillet", 12.0f) * S - 3.0f * S), bx1 = snap(sq ? cx + r : cx + off + sr * 0.8f + TUNE("mm.dial_fillet", 12.0f) * S + 3.0f * S), bins = 2.6f * S;   // the square : the frame's width   // both fillets land on the band
        // the POSITION in the band (the user 2026-11-25 : "lower, between the moon and the time") : a third compartment at
        // the centre, under the S, sized to the grid square -- in the modules style (round)
        char posTxt[12] = ""; bool posBand = false;
        if (c.mmGrid && c.mmDayDial == 2 && !sq) {
            const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
            if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) { sprintf(posTxt, "%c-%d", 'A' + gc, gr + 1); posBand = true; }   // ONE wheel for the whole square (), as the game writes it : "H-9"
        }
        const float posHalf = posBand && tf ? snap(drum_width(tf, posTxt, pSz, S) * 0.5f + 3.0f * S) : 0.0f;
        const float lc0 = bx0 + bins, lc1 = cx - (posBand ? posHalf + 4.0f * S : 7.0f * S), rc0 = cx + (posBand ? posHalf + 4.0f * S : 7.0f * S), rc1 = bx1 - bins;   // the compartments
        // THE PLINTH (the user 2026-11-25 : "rethink how it was forged, credible in FFXI -- steampunk or magic in its
        // making" ; "the cartouches still overflow") : Vana'diel's craft is Bastok's machinery driven by crystals, so the
        // instrument stands on a FORGED PLINTH -- a straight base, its sides rising in concave fillets into the ring like a
        // Bastokan clock's foot -- and its three windows are lit from within by CRYSTAL light, two cut crystal shards
        // between them. The windows sit on ONE row : the room under the ring only grows toward the sides, nothing can
        // overflow. Moon (left) | grid square (centre, under the S) | hour (right). (Round, modules style.)
        const bool foot = !sq && c.mmDayDial == 2;
        float cwX[3] = { (lc0 + lc1) * 0.5f, cx, (rc0 + rc1) * 0.5f }, cwY[3] = { bmid, bmid, bmid }, cwH[3] = { 0, posHalf, 0 }, cwHH = 0.0f;
        if (foot && (bandMoon || bandDay || posBand)) {
            const float wh = snap(pSz + 6.0f * S), bz = snap(4.0f * S), gap = snap(TUNE("mm.foot_gap", 15.0f) * S);   // the windows apart : three instruments, not one strip
            const float hwM = snap(drum_width(tf, "Full 99d", pSz, S) * 0.5f + 3.0f * S);   // the windows hold their wheels
            const float hwT = snap(drum_width(tf, "00:00", pSz, S) * 0.5f + 3.0f * S);
            const float hwP = posBand ? posHalf : 0.0f;
            const float yB = snap(cy + r + 2.0f * S + wh + bz * 1.6f);                       // the base line
            const float yC = snap(yB - bz - wh * 0.5f);                                      // the windows' row
            const float edge0 = TUNE("mm.foot_edge", 7.0f) * S;                            // position | hour : the moon lives in its own dial now
            const float hwD = snap(drum_width(tf, "00", pSz, S) * 0.5f + 3.0f * S);          // the moon's day counter
            const float hwS = hwD > hwT ? hwD : hwT;
            const float half = hwP + gap + 2.0f * hwS + edge0;                               // days | square | hour, the square under the S
            (void)hwM;
            const float W = half + 2.0f * S, xa = r * 0.78f, ya = cy + sqrtf(r * r - xa * xa) - 3.0f * S;   // where the fillets leave the ring
            const float ySide = yB - 4.0f * S, ch = 3.5f * S;                                 // the sides' foot, the base's chamfer
            auto topAt = [&](float dx) {                                                     // the plinth's top : under the ring, then the fillet
                const float ax = fabsf(dx);
                if (ax <= xa) return cy + sqrtf(r * r - ax * ax) - 3.0f * S;
                const float t = (ax - xa) / (W - xa); return ya + (ySide - ya) * t * t;        // concave : flaring out into the base
            };
            auto botAt = [&](float dx) { const float e = W - fabsf(dx); return yB - (e < ch ? ch - e : 0.0f); };
            const int NC = 72;
            auto slab = [&](float g, u32 cT, u32 cB) {
                VtxC fv[2 * (NC + 1)];
                for (int i = 0; i <= NC; ++i) {
                    const float dx = -W + 2.0f * W * (float)i / NC, tY = topAt(dx) - g, bY = botAt(dx) + g, xg = dx * (W + g) / W;
                    fv[2 * i] = { cx + xg - 0.5f, tY - 0.5f, 0, 1, cT }; fv[2 * i + 1] = { cx + xg - 0.5f, bY - 0.5f, 0, 1, cB };
                }
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * NC, fv, sizeof(VtxC));
            };
            color_state(dev);
            const bool body = TUNE("mm.foot_body", 0.0f) > 0.5f;
            s_tubes = !body;                            // the plinth's metal : OFF for now (the user 2026-11-25 : "remove the foot, keep the hour and the position")
            if (body) {
            rrect_glow(dev, cx - W, yB - 6.0f * S, 2.0f * W, 6.0f * S + 1.0f * S, snap(3.0f * S), 0x80000000u, snap(4.0f * S));   // its shadow on the world
            slab(1.3f * S, g_dm.edge, g_dm.edge);                                             // the dark edge
            slab(0.0f, dm_toward(g_dm.bot, 0xFF000000u, 0.20f), dm_toward(g_dm.bot, 0xFF000000u, 0.50f));   // the forged metal
            for (int i = 0; i < NC; ++i) {                                                   // its anti-aliased outline, and the fillets' lit edge
                const float d0 = -W + 2.0f * W * (float)i / NC, d1 = -W + 2.0f * W * (float)(i + 1) / NC;
                if (fabsf(d0) > xa && fabsf(d1) > xa) seg_soft(dev, cx + d0, topAt(d0), cx + d1, topAt(d1), 1.0f, MA(0x90, g_dm.lite));
                seg_soft(dev, cx + d0, botAt(d0) + 0.6f, cx + d1, botAt(d1) + 0.6f, 1.1f, g_dm.edge);
            }
            grad_quad(dev, cx - W + ch, yB - 2.6f * S, 2.0f * (W - ch), 1.0f, MA(0x70, g_dm.lite), MA(0x70, g_dm.lite), MA(0x70, g_dm.lite), MA(0x70, g_dm.lite));   // an engraved filet along the base
            }
            // the windows : one row, the position under the S, the moon and the hour on either side
            const float x3[3] = { cx - W + 2.0f * S + edge0 + hwD, cx, cx + W - 2.0f * S - edge0 - hwT }, hw3[3] = { hwD, hwP, hwT };
            const bool on3[3] = { bandMoon && body, posBand, bandDay };   // no plinth : no day counter (the moon pod shows the phases)
            for (int k = 0; k < 3; ++k) {
                if (!on3[k] || hw3[k] <= 0.0f) continue;
                cwX[k] = snap(x3[k]); cwY[k] = yC; cwH[k] = hw3[k];
                const float x0 = cwX[k] - hw3[k], x1 = cwX[k] + hw3[k], y0 = yC - wh * 0.5f, y1 = yC + wh * 0.5f, cc = 3.0f * S;
                auto oct = [&](float gg, u32 col) {
                    const float o8[16] = { x0 + cc - gg, y0 - gg, x1 - cc + gg, y0 - gg, x1 + gg, y0 + cc - gg, x1 + gg, y1 - cc + gg,
                                           x1 - cc + gg, y1 + gg, x0 + cc - gg, y1 + gg, x0 - gg, y1 - cc + gg, x0 - gg, y0 + cc - gg };
                    fill_poly_aa(dev, o8, 8, col);
                };
                (void)oct;                                                                  // (no frame : each wheel group is its own aperture in the plinth)
                cwHH = wh * 0.5f - 0.6f * S;
            }
            for (int k = 0; k < 2; ++k) {                                                    // between the windows : a double groove cut in the metal
                if (!on3[1] || !body) break;
                const float sx = k == 0 ? ((x3[0] + hwD) + (cx - hwP)) * 0.5f : ((cx + hwP) + (x3[2] - hwT)) * 0.5f, hh = wh * 0.42f;
                for (int g2 = -1; g2 <= 1; g2 += 2) {
                    const float gx = sx + (float)g2 * 1.6f * S;
                    seg_soft(dev, gx, yC - hh, gx, yC + hh, 1.0f * S, MA(0xC0, g_dm.edge));
                    seg_soft(dev, gx + 0.9f * S, yC - hh, gx + 0.9f * S, yC + hh, 0.8f * S, MA(0x60, g_dm.lite));
                }
            }
            if (!body) {   // THE TUBES (the user 2026-11-25 : "the hour at the top, centred ; the position at the bottom, centred, in
                           // the same kind of tube as the other modules") : a boss milled out of the bezel (pod_housing), the
                           // wheels in their aperture inside it -- the hour above the N, the grid square under the S
                const float D = r * TUNE("mm.pod_depth", 0.235f), Ri = r - 3.0f * S, Ro = r + D, rmT = (Ri + Ro) * 0.5f, rwT = r + D * 0.5f;
                const u32 hbT = dm_toward(g_dm.bot, 0xFF000000u, TUNE("mm.pod_dark", 0.25f));
                cwHH = fminf(wh * 0.5f - 0.6f * S, D * 0.5f - 2.5f * S);
                s_tubeRw = rwT;
                for (int k = 1; k <= 2; ++k) {
                    if (!on3[k] || hw3[k] <= 0.0f) continue;
                    const float th = k == 2 ? s_hourTh : s_posTh;
                    (void)rmT; (void)hbT; (void)Ri; (void)Ro;                // (the housing : the collar)
                    cwX[k] = snap(cx); cwY[k] = snap(cy + sinf(th) * rwT); cwH[k] = hw3[k];
                }
            }
            color_state(dev);
            if (posBand) { if (s_tubes) drum_arc(dev, tf, 1, cx, cy, s_posTh, s_tubeRw, cwHH, posTxt, pSz, S); else drum_text(dev, tf, 1, cwX[1], cwY[1], cwH[1] - 0.6f * S, cwHH, posTxt, pSz, S); }
        }
        if (!foot && (bandMoon || bandDay)) {
            color_state(dev);
            const float rad = snap(3.0f * S);
            color_state(dev);
            rrect_glow(dev, bx0, by0 + 1.0f * S, bx1 - bx0, bandH, rad, 0x80000000u, snap(3.0f * S));
            // CAST JOINT : the round subdial and the straight band are ONE piece. Every dark rim first (the subdials', the
            // fillets', the band's), then all the brass over them -- no dark line between the two -- and at each side of a
            // junction a FILLET : a concave curve flaring from the subdial's round into the band's straight top edge
            auto fillet = [&](float sx, float sy, float R, float L, float f, u32 col) {
                const float dy = L - sy; if (fabsf(dy) >= R) return;
                const float hc = sqrtf(R * R - dy * dy), dy1 = L - f - sy; if (fabsf(dy1) >= R) return;
                const float h1 = sqrtf(R * R - dy1 * dy1);
                for (int sd = -1; sd <= 1; sd += 2) {
                    if (sq && (sd > 0) == (sx > cx)) continue;                        // the square : no fillet past the frame's side
                    const float Cx = sx + sd * hc, Cy = L, P0x = sx + sd * (hc + f), P0y = L, P1x = sx + sd * h1, P1y = L - f;
                    const int N = 10; float px[N + 1], py[N + 1];
                    for (int i = 0; i <= N; ++i) {
                        const float tq = (float)i / N, uq = 1.0f - tq;
                        px[i] = uq * uq * P0x + 2.0f * uq * tq * Cx + tq * tq * P1x;
                        py[i] = uq * uq * P0y + 2.0f * uq * tq * Cy + tq * tq * P1y;
                    }
                    static ShapeBatch fb(0); fb.dev = dev; fb.n = 0;          // ONE draw per fillet (was 20)
                    for (int i = 0; i < N; ++i) fb.tri(Cx, Cy, px[i], py[i], px[i + 1], py[i + 1], col);
                    for (int i = 0; i < N; ++i) fb.seg(px[i], py[i], px[i + 1], py[i + 1], 1.0f, col);   // its AA silhouette
                    fb.flush();
                }
            };
            const float filR = TUNE("mm.dial_fillet", 12.0f) * S;
            for (int k = 0; k < 2; ++k) {                                                        // the dark : subdial rims + fillet rims
                if (k == 0 ? !bandMoon : (!bandDay || (c.mmDayDial == 2 && !sq))) continue;
                const float sx2 = k == 0 ? cx - off : cx + off, sy2 = cy + off;
                rrect_glow(dev, sx2 - sr - 2.0f * S, sy2 - sr - 1.0f * S, 2.0f * sr + 4.0f * S, 2.0f * sr + 4.0f * S, sr + 2.0f * S, 0x80000000u, snap(3.0f * S));
                color_state(dev);
                disc(dev, sx2, sy2, sr + 2.4f * S, g_dm.edge);
                fillet(sx2, sy2, sr + 2.4f * S, by0 - 1.2f * S, filR + 1.0f * S, g_dm.edge);
            }
            rrect(dev, bx0 - 1.2f * S, by0 - 1.2f * S, bx1 - bx0 + 2.4f * S, bandH + 2.4f * S, rad + 1.2f * S, g_dm.edge, g_dm.edge);
            rrect(dev, bx0, by0, bx1 - bx0, bandH, rad, g_dm.top, g_dm.bot);
            grad_quad(dev, bx0 + rad, by0 + 1.0f, bx1 - bx0 - 2.0f * rad, 1.0f, MA(0x90, g_dm.glint), MA(0x90, g_dm.glint), MA(0x90, g_dm.glint), MA(0x90, g_dm.glint));
            for (int k = 0; k < 2; ++k) {                                                        // the brass fillets, over the band's top
                if (k == 0 ? !bandMoon : (!bandDay || (c.mmDayDial == 2 && !sq))) continue;
                fillet(k == 0 ? cx - off : cx + off, cy + off, sr + 1.6f * S, by0, filR, g_dm.body);
            }
            grad_quad(dev, bx0 + rad, by0 + bandH - 1.0f, bx1 - bx0 - 2.0f * rad, 1.0f, MA(0x70, g_dm.shade), MA(0x70, g_dm.shade), MA(0x70, g_dm.shade), MA(0x70, g_dm.shade));
            for (int k = 0; k < 2; ++k) {
                if (k == 0 ? !bandMoon : !bandDay) continue;
                const float a0 = k == 0 ? lc0 : rc0, a1 = k == 0 ? lc1 : rc1, iy = by0 + bins;
                rrect(dev, a0 - 0.8f * S, iy - 0.8f * S, a1 - a0 + 1.6f * S, bandH - 2.0f * bins + 1.6f * S, snap(1.8f * S), g_dm.dark, g_dm.lite);   // the chamfer
                rrect(dev, a0, iy, a1 - a0, bandH - 2.0f * bins, snap(1.4f * S), 0xFF0F0C08u, 0xFF1E1810u);                                         // the recess
            }
            if (posBand) {                                                                      // the position's compartment, its text
                const float a0 = cx - posHalf, a1 = cx + posHalf, iy = by0 + bins;
                rrect(dev, a0 - 0.8f * S, iy - 0.8f * S, a1 - a0 + 1.6f * S, bandH - 2.0f * bins + 1.6f * S, snap(1.8f * S), g_dm.dark, g_dm.lite);
                rrect(dev, a0, iy, a1 - a0, bandH - 2.0f * bins, snap(1.4f * S), 0xFF0F0C08u, 0xFF1E1810u);
                plateText(cx, bmid, posTxt, 0xFFF0D898u, -1);
            } else {
                disc(dev, cx, bmid, 4.2f * S, g_dm.edge); disc(dev, cx, bmid, 3.4f * S, g_dm.lite);    // the gilt cabochon
                disc(dev, cx - 1.0f * S, bmid - 1.0f * S, 1.2f * S, 0xC0FFF6DCu);
            }
            color_state(dev);
        }
        PERF_END("mm.band");
        PERF_BEGIN("mm.moon");
        // ---- BL : the MOON, large, in a starry night ; its line on the band : the next event, in words ----
        if (c.mmClock && c.mmClkMoon) {
            const float srM = foot ? sr * TUNE("mm.moon_k", 1.45f) : sr;            // the phase dial needs room : bigger, pushed out along its diagonal
            const float offM = off + (srM - sr) * 0.62f, x = cx - offM, y = cy + offM - (foot ? srM * TUNE("mm.moon_up", 0.40f) : 0.0f);   // raised : clear of the plinth
            const bool fullFirst = v.daysToFull < v.daysToNew;
            const int dd = fullFirst ? v.daysToFull : v.daysToNew;
            char mb[32];
            (void)0;                                                                      // (day 0 : "00", the cylinders stay -- an odometer never loses a wheel)
            sprintf(mb, fullFirst ? "Full %02dd" : "New %02dd", dd < 0 ? 0 : dd);   // New / Full in every language (the user 2026-11-25 : everyone reads them)
            const float ty = bmid;
            if (!foot) bezel(x, y, 0xFF101C3Cu, true);
            else if (false) {                                                        // (the round phase dial : replaced by the MOON POD, upper left)
                color_state(dev);
                rrect_glow(dev, x - srM - 2.0f * S, y - srM - 1.0f * S, 2.0f * srM + 4.0f * S, 2.0f * srM + 4.0f * S, srM + 2.0f * S, 0x80000000u, snap(3.0f * S));
                color_state(dev);
                disc(dev, x, y, srM + 2.4f * S, g_dm.edge);
                disc(dev, x, y, srM + 1.6f * S, g_dm.body);
                ring_sector(dev, x, y, srM + 0.3f * S, srM + 1.6f * S, PI, 1.5f * PI, MA(0xA0, g_dm.glint));
                ring_sector(dev, x, y, srM + 0.3f * S, srM + 1.6f * S, 0.0f, 0.5f * PI, MA(0x90, g_dm.shade));
                disc(dev, x, y, srM + 0.3f * S, g_dm.dark);
            }
            color_state(dev);
            if (!foot) { static const float ST[6][2] = { {-0.62f,-0.30f}, {0.58f,-0.48f}, {-0.35f,-0.70f}, {0.70f,0.10f}, {-0.72f,0.22f}, {0.30f,-0.75f} };
            for (int k = 0; k < 6; ++k) disc(dev, snap(x + ST[k][0] * sr), snap(y + ST[k][1] * sr), 0.7f, 0xB0FFF6DCu); }
            if (!foot) {
                draw_moon_sphere(dev, x, y, sr, v.moonPct, v.waning);              // a lit sphere by the real phase, the WHOLE face
                glass_dome(dev, x, y, sr, 0xFF9CB8FFu, 0.35f);
                color_state(dev);
                plateText(cwX[0], ty, mb, fullFirst ? 0xFFF8E6B0u : 0xFFC8D6F4u, -1);
            } else {
                // THE MOON-PHASE DIAL (the user 2026-11-25) : the moon in the middle ; around it the LUNAR CYCLE -- a ring with
                // the FULL moon marked at the top (a bright disc) and the NEW at the bottom (a dark one), a needle at where the
                // cycle stands (waxing up the left, waning down the right) ; the days to the next of the two on a wheel
                if (false) {                                                         // (the round phase dial : replaced by the MOON POD)
                // A PAPER DIAL OF THE PHASES (the user 2026-11-25 : "steampunk : no 3D moon -- a paper disc with all the moon's
                // phases, like a watch, a hand pointing the present phase") : the whole face is parchment, the eight phases
                // inked round it (new at the bottom, full at the top, waxing up the left, waning down the right), a chased
                // metal HAND on a pivot pointing where the cycle stands
                const float pct = (float)v.moonPct / 100.0f, cyc = v.waning ? 0.5f + (1.0f - pct) * 0.5f : pct * 0.5f;   // 0 new .. 0.5 full .. 1 new
                const float face = srM - 0.5f * S;
                const float rmP = face * (s_moonDialTex ? 0.74f : 0.64f), pr = face * (s_moonDialTex ? 0.11f : 0.16f);   // the phases' ring : the texture's moons sit at 74 % of the radius
                if (s_moonDialTex) {                                                     // THE TEXTURE (an inked almanac, the user 2026-11-25)
                    tex_state(dev, s_moonDialTex);
                    tquad(dev, snap(x - face), snap(y - face), 2.0f * face, 2.0f * face, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
                    dSetTex(dev, 0, 0);
                    color_state(dev);
                } else {
                disc(dev, x, y, face, 0xFFDCC48Eu);                                     // the parchment face (fallback : inked by code)
                ring_sector(dev, x, y, face * 0.80f, face, 0.0f, 2.0f * PI, 0x30603A10u);   // darker toward the bezel
                ring_sector(dev, x, y, face * 0.86f - 0.5f * S, face * 0.86f + 0.5f * S, 0.0f, 2.0f * PI, 0x903A2410u);   // an inked circle
                for (int k = 0; k < 8; ++k) {                                           // the phases in INK
                    const float a2 = PI * 0.5f + (float)k / 8.0f * 2.0f * PI;            // new at the bottom, turning up the left
                    const float px = x + cosf(a2) * rmP, py = y + sinf(a2) * rmP;
                    const u32 ink = 0xFF2E1C0Cu;
                    {   // INK ON PAPER (the user 2026-11-25 : "just the ink it takes -- a black disc for the new moon, a circle's
                        // border for the full, the same for the others") : the paper is the light ; a fine inked circle, the dark
                        // part filled in ink up to the terminator, the terminator drawn as one smooth stroke
                        const float l = (float)(k <= 4 ? k : 8 - k) / 4.0f, side = k <= 4 ? 1.0f : -1.0f;   // lit fraction, waxing lit on the right
                        if (k == 0) disc(dev, px, py, pr, ink);                          // NEW : all ink
                        else {
                            if (l < 0.999f) {                                            // the dark part : from the dark limb to the terminator
                                const float e = 1.0f - 2.0f * l, rr3 = pr - 0.5f;
                                const int NY = 24; VtxC tv[6 * NY]; int nt = 0;
                                for (int j = 0; j < NY; ++j) {
                                    const float ya = -rr3 + 2.0f * rr3 * (float)j / NY, yb = -rr3 + 2.0f * rr3 * (float)(j + 1) / NY;
                                    const float ha = sqrtf(fmaxf(0.0f, rr3 * rr3 - ya * ya)), hb = sqrtf(fmaxf(0.0f, rr3 * rr3 - yb * yb));
                                    const VtxC A = { px - side * ha - 0.5f, py + ya - 0.5f, 0, 1, ink }, B = { px + side * e * ha - 0.5f, py + ya - 0.5f, 0, 1, ink };
                                    const VtxC C = { px - side * hb - 0.5f, py + yb - 0.5f, 0, 1, ink }, D = { px + side * e * hb - 0.5f, py + yb - 0.5f, 0, 1, ink };
                                    tv[nt++] = A; tv[nt++] = B; tv[nt++] = C; tv[nt++] = B; tv[nt++] = D; tv[nt++] = C;
                                }
                                dDrawUP(dev, D3DPT_TRIANGLELIST, (u32)(nt / 3), tv, sizeof(VtxC));
                                float lx = 0, ly = 0;                                    // the terminator : one smooth inked stroke
                                for (int j = 0; j <= 16; ++j) {
                                    const float yy = -rr3 + 2.0f * rr3 * (float)j / 16.0f, hh2 = sqrtf(fmaxf(0.0f, rr3 * rr3 - yy * yy));
                                    const float tx = px + side * e * hh2, ty = py + yy;
                                    if (j) seg_soft(dev, lx, ly, tx, ty, 1.1f, ink);
                                    lx = tx; ly = ty;
                                }
                            }
                            ring_sector(dev, px, py, pr - 0.9f, pr, 0.0f, 2.0f * PI, ink);   // the inked circle : a fine smooth line
                        }
                    }
                    const float ta = a2 + PI / 8.0f;                                    // an inked tick between two phases
                    seg_soft(dev, x + cosf(ta) * face * 0.86f, y + sinf(ta) * face * 0.86f, x + cosf(ta) * face * 0.97f, y + sinf(ta) * face * 0.97f, 0.9f * S, 0xB03A2410u);
                }
                }
                {   // the HAND : chased metal, a counterweight, a pivot
                    const float na = PI * 0.5f + cyc * 2.0f * PI, ux2 = cosf(na), uy2 = sinf(na), nx2 = -uy2, ny2 = ux2;
                    const float tip = rmP - pr * 1.15f, tail = face * 0.22f, w0 = 2.6f * S;   // reaches the moons' inner edge
                    const float hs[8] = { x + ux2 * tip + 0.7f, y + uy2 * tip + 1.0f, x + nx2 * w0 + 0.7f, y + ny2 * w0 + 1.0f,
                                          x - ux2 * tail + 0.7f, y - uy2 * tail + 1.0f, x - nx2 * w0 + 0.7f, y - ny2 * w0 + 1.0f };
                    fill_poly_aa(dev, hs, 4, 0x80000000u);                              // its shadow on the paper
                    const float hd[8] = { x + ux2 * tip, y + uy2 * tip, x + nx2 * w0, y + ny2 * w0, x - ux2 * tail, y - uy2 * tail, x - nx2 * w0, y - ny2 * w0 };
                    const float ho[8] = { x + ux2 * (tip + 1.0f * S), y + uy2 * (tip + 1.0f * S), x + nx2 * (w0 + 1.0f * S), y + ny2 * (w0 + 1.0f * S),
                                          x - ux2 * (tail + 1.0f * S), y - uy2 * (tail + 1.0f * S), x - nx2 * (w0 + 1.0f * S), y - ny2 * (w0 + 1.0f * S) };
                    fill_poly_aa(dev, ho, 4, 0xFF1A120Au);                                // a dark edge : it reads on the parchment AND on the night band
                    fill_poly_aa(dev, hd, 4, 0xFFB88A3Au);                                // gilt brass
                    const float hl[6] = { x + ux2 * tip, y + uy2 * tip, x + nx2 * w0, y + ny2 * w0, x - ux2 * tail, y - uy2 * tail };
                    fill_poly_aa(dev, hl, 3, 0xFFF0D088u);                                // its lit facet
                    disc(dev, x, y, 2.8f * S, 0xFF1A120Au); disc(dev, x, y, 2.0f * S, 0xFFC8A050u);   // the pivot, a gilt cap
                }
                color_state(dev);
                glass_dome(dev, x, y, srM, 0xFF9CB8FFu, 0.35f);
                color_state(dev);
                }
                char db[8]; sprintf(db, "%02d", dd < 0 ? 0 : (dd > 99 ? 99 : dd));       // the days to the next new / full : the plinth's left window
                if (cwHH > 0.0f && cwH[0] > 0.0f) drum_text(dev, tf, 0, cwX[0], cwY[0], cwH[0] - 0.6f * S, cwHH, db, pSz, S);
            }
        }
        PERF_END("mm.moon");
        PERF_BEGIN("mm.globe");
        // ---- BR : the DAY WHEEL (the user 2026-11-25 : "a wheel of all the elements, in coloured quarters, with a needle,
        //      and the dial turns") : the eight elements in their week order, each a quarter of its colour on a ring ; a
        //      gilt needle fixed at 12 o'clock points at TODAY ; the wheel turns with the hours, so tomorrow's quarter
        //      creeps toward the needle and reaches it at midnight. Today's element, large, at the hub. Its plate : the
        //      time alone, centred ----
        if (c.mmClock && (c.mmClkTime || c.mmClkDay)) {
            const float x = cx + off, y = cy + off;
            char tb[16]; tb[0] = 0; if (c.mmClkTime) sprintf(tb, "%02d:%02d", v.hh, v.mm);
            const float ty = bmid;
            const bool pod = c.mmDayDial == 2 && !sq;                                     // the day POD (round only) replaces this subdial
            if (!pod) bezel(x, y, 0xFF0E1218u, true);
            if (c.mmClkDay && !pod) {
                // the DAY SPHERE in a water-filled dial : the face a deep teal, the ball floating in it, a glass gleam over all
                const float frac = ((float)v.hh * 60.0f + (float)v.mm) / 1440.0f;
                const float ri = sr * 0.70f;                                                  // (the needle's tip reaches it)
                color_state(dev);
                rrect(dev, x - sr, y - sr, 2.0f * sr, 2.0f * sr, sr, 0xFF16303Au, 0xFF0A161Eu);   // the water, darker deep down
                if (wxTex && c.mmDayDial != 0) draw_day_carousel(dev, x, y, sr, di, frac, wxTex);   // Minimap > Clock > Day dial : the icons' carousel
                else       draw_day_sphere(dev, x, y, sr, di, frac, f.t);                     // (the enamel globe while the HD atlas loads)
                color_state(dev);
                // the NEEDLE : a pointer fixed at 12 o'clock, from above the case's rim down into the ring, in the theme's metal
                // with a dark outline (a thin light needle vanished against the quarters)
                const float nTop = y - sr - 3.0f * S, nTip = y - ri * 0.80f, nw = 3.6f * S, o = 1.1f * S;
                const float ol[6] = { x - nw - o, nTop - o, x + nw + o, nTop - o, x, nTip + o * 1.8f };
                fill_poly_aa(dev, ol, 3, 0xE0000000u);                                // its dark outline / shadow
                const float nd[6] = { x - nw, nTop, x + nw, nTop, x, nTip };
                fill_poly_aa(dev, nd, 3, g_dm.lite | 0xFF000000u);
                const float hl[6] = { x, nTop + 0.6f * S, x + nw * 0.75f, nTop + 0.6f * S, x, nTip - 1.5f * S };
                fill_poly_aa(dev, hl, 3, MA(0xB0, g_dm.glint));                       // its lit facet
            }
            if (tb[0] && foot && cwHH > 0.0f && s_tubes) drum_arc(dev, tf, 2, cx, cy, s_hourTh, s_tubeRw, cwHH, tb, pSz, S);
            else if (tb[0] && foot && cwHH > 0.0f) drum_text(dev, tf, 2, cwX[2], cwY[2], cwH[2] - 0.6f * S, cwHH, tb, pSz, S);
            else if (tb[0]) plateText((rc0 + rc1) * 0.5f, ty, tb, 0xFFF4EEDFu, -1);
        }
        color_state(dev);
    }
    PERF_END("mm.globe");
    // the compass's SHADOW on the modules behind it : a soft dark rim just outside its case
    color_state(dev);
    if (!sq) disc_glow(dev, cx, cy, r, 0x9A000000u, 6.0f * S);
    else rrect_glow(dev, cx - r, cy - r + 1.5f * S, 2.0f * r, 2.0f * r, snap(6.0f * S), 0x9A000000u, snap(6.0f * S));
    color_state(dev);
    return;
    }
    {   // the weather module, on the FACE (pass 1)
        const float sr = snap(r * TUNE("mm.dial_sub_k", 0.165f)), rr = r + sr + TUNE("mm.dial_sub_gap", 1.5f) * S;
        const float off = sq ? r - (r - rc) * 0.5f : 0.7071f * rr, x = cx + off, y = cy - off;
        if (c.mmClock && (c.mmClkTime || c.mmClkDay) && !(s_skyPod && !sq && c.mmDayDial == 2)) {   // (the SKY MODULE replaces the capsules)
            // THE WEATHER MODULE (the user 2026-11-25 : "the day/night module is welded to the map ; at the WELD the two
            // modules are stowed on top of the main dial, and they unfold toward the day/night module, in the right
            // orientation") : drawn on the compass's face. Folded : the two wells stacked on the ring at the weld. A weather :
            // they slide out along the axis compass centre -> sky dial, spread apart across it (a hinge opening), then the
            // PARCHMENT label with the element pops in (the first for a single weather, both for a double). No weather : the
            // reverse. A new element : the old label out, the new one in. Nothing animates on the first frame.
            {
                const int wid = mm_weather(g.weather);
                static const int WCELL[8] = { 0, 2, 1, 3, 4, 5, 6, 7 };             // weather pair -> the element's cell
                const int cellNow = (wid >= 4 && wid <= 19) ? WCELL[(wid - 4) / 2] : -1, nNow = cellNow < 0 ? 0 : ((wid & 1) ? 2 : 1);
                static int shownCell[2] = { -2, -2 }, want[2] = { -2, -2 }; static unsigned tChange[2] = { 0, 0 };
                const unsigned nowMs = GetTickCount();
                for (int q = 0; q < 2; ++q) {
                    const int w2 = q < nNow ? cellNow : -1;
                    if (shownCell[q] == -2) { shownCell[q] = want[q] = w2; tChange[q] = 0; }   // the first frame : in place
                    else if (w2 != want[q]) { want[q] = w2; tChange[q] = nowMs; }
                }
                const float rs = sr * TUNE("mm.caps_r", 0.60f);          // bigger : the element must read at a glance
                // EACH CAPSULE ITS OWN FOLD (the user 2026-11-25 : "side by side from the start ; a single element : only one
                // unfolds") : p[q] runs 0 (stowed) -> 1 (out) in ~0.45 s ; the slide 0..0.7, its label 0.7..1
                static float pq[2] = { -1.0f, -1.0f }; static unsigned lastMs = 0;
                const float dtS = lastMs ? (float)(nowMs - lastMs) / 1000.0f : 0.0f; lastMs = nowMs;
                auto cl = [](float v2) { return v2 < 0.0f ? 0.0f : (v2 > 1.0f ? 1.0f : v2); };
                float depQ[2], labQ[2], lidQ[2];
                for (int q = 0; q < 2; ++q) {
                    const float tgt = q < nNow ? 1.0f : 0.0f;
                    if (pq[q] < 0.0f) pq[q] = tgt;
                    const float st = (dtS > 0.1f ? 0.1f : dtS) / 0.60f;
                    pq[q] = pq[q] < tgt ? (pq[q] + st > tgt ? tgt : pq[q] + st) : (pq[q] - st < tgt ? tgt : pq[q] - st);
                    lidQ[q] = cl(pq[q] / 0.30f);                                     // the LID opens first (the user : "a cover when stowed")...
                    float d = cl((pq[q] - 0.25f) / 0.50f); depQ[q] = d * d * (3.0f - 2.0f * d);   // ...then the capsule slides out...
                    labQ[q] = cl((pq[q] - 0.75f) / 0.25f);                          // ...then its label
                }
                {
                // the axis : compass centre -> sky dial ; the WELD is where the ring meets the sky dial. Folded on the ring there,
                // the carrier slides out toward the sky dial and its wells spread across the axis
                const float ax0 = x - cx, ay0 = y - cy, aL = sqrtf(ax0 * ax0 + ay0 * ay0), ux = ax0 / aL, uy = ay0 / aL, tx = -uy, ty = ux;
                const float D0 = r - rs * TUNE("mm.slot_in", 0.35f), D1 = r + rs * TUNE("mm.slot_reach", 0.65f);
                const float half = rs * TUNE("mm.slot_gap", 1.10f);              // side by side across the axis, always
                float OX[2], OY[2];
                (void)D1;
                const float aq[2] = { TUNE("mm.caps_a0", 3.30f), TUNE("mm.caps_a1", 1.05f) };   // around the sky dial : its left, its lower right
                const float Rq = sr + rs * TUNE("mm.caps_out", 0.70f);
                for (int q = 0; q < 2; ++q) {
                    const float sx0 = cx + ux * D0 + (q == 0 ? -1.0f : 1.0f) * tx * half, sy0 = cy + uy * D0 + (q == 0 ? -1.0f : 1.0f) * ty * half;   // stowed at the weld
                    const float fx = x + cosf(aq[q]) * Rq, fy = y + sinf(aq[q]) * Rq;                                                               // its corner
                    OX[q] = sx0 + (fx - sx0) * depQ[q]; OY[q] = sy0 + (fy - sy0) * depQ[q];
                }
                color_state(dev);
                for (int q = 0; q < 2; ++q)
                    rrect_glow(dev, OX[q] - rs - 1.5f * S, OY[q] - rs - 0.5f * S, 2.0f * rs + 3.0f * S, 2.0f * rs + 3.0f * S, rs + 1.5f * S, 0x90000000u, snap(2.5f * S));
                color_state(dev);
                // the two wells, cast as one : the dark edges, a bridge, then the metal
                const float bw = rs * 0.55f;
                auto bridge = [&](float grow, u32 col) {
                    if (depQ[0] > 0.02f || depQ[1] > 0.02f) return;                // joined only while both are stowed at the weld
                    const float dx = OX[1] - OX[0], dy = OY[1] - OY[0], L = sqrtf(dx * dx + dy * dy); if (L < 1.0f) return;
                    const float nx = -dy / L * (bw + grow), ny = dx / L * (bw + grow);
                    const float q4[8] = { OX[0] + nx, OY[0] + ny, OX[1] + nx, OY[1] + ny, OX[1] - nx, OY[1] - ny, OX[0] - nx, OY[0] - ny };
                    fill_poly_aa(dev, q4, 4, col);
                };
                (void)bridge;                                                          // (all glass now : no metal setting, no bridge)
                for (int q = 0; q < 2; ++q) {
                    const float ox = OX[q], oy = OY[q];
                    glass_bead_under(dev, ox, oy, rs, S);
                    // the LABEL : out (the old one shrinks away, 0.12 s), then in (the new one pops, 0.22 s, a small overshoot)
                    float sc = 0.0f; int cell = shownCell[q];
                    const float lab = labQ[q];
                    if (lab <= 0.0f) cell = -1;                                     // the label comes once its capsule is out
                    if (tChange[q]) {
                        const float t = (float)(nowMs - tChange[q]) / 1000.0f;
                        const bool hadOld = shownCell[q] >= 0 && shownCell[q] != want[q];
                        const float tOut = hadOld ? 0.12f : 0.0f;
                        if (t < tOut) { sc = 1.0f - t / tOut; cell = shownCell[q]; }
                        else {
                            cell = want[q];
                            const float k = (t - tOut) / 0.22f;
                            if (k >= 1.0f) { shownCell[q] = want[q]; tChange[q] = 0; sc = 1.0f; }
                            else { const float u = k - 1.0f; sc = 1.0f + 2.7f * u * u * u + 1.7f * u * u; }   // ease-out-back
                        }
                    } else sc = 1.0f;
                    if (cell >= 0 && sc > 0.01f) {
                        const float lr = rs * sc * lab;                              // GLASS (the user 2026-11-25) : the element alone in the black glass
                        if (wxTex) {
                            const float isz = snap(lr * 1.95f);                     // the element fills the capsule
                            tex_state(dev, wxTex);
                            tquad(dev, snap(ox - isz * 0.5f), snap(oy - isz * 0.5f), isz, isz, cell / 8.0f, (cell + 1) / 8.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
                            dSetTex(dev, 0, 0);
                        }
                        color_state(dev);
                    }
                    glass_bead_over(dev, ox, oy, rs, S);
                }
                color_state(dev);
                }
            }
        }
    }
    // -- the cardinal letters, engraved in the brass band -- LAST : nothing of the case may cover them --
    if (fo) {
        const float rm = (rc + r) * 0.5f, csz = snap(TUNE("mm.dial_card", 13.0f) * S * clampf(c.mmCardSz, 0.5f, 2.0f) / 0.7f);
        if (!sq && c.mmDayDial == 2 && c.mmClock && c.mmClkDay && fabsf(pod_geo(r, S).th0 + 1.5708f) < 0.05f) draw_day_pod_needle(dev, cx, cy, rc, r, S, csz);   // the day pod's needle, out of the N (only when the pod sits above it)
        static const char* NM[4] = { "N", "E", "S", "W" };
        static const float DX[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, DY[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
        fo->begin(dev);
        for (int i = 0; i < 4; ++i)
            fo->draw_c(dev, snap(cx + DX[i] * rm), snap(cy + DY[i] * rm), NM[i], csz, i == 0 ? 0xFFFF5A44u : 0xFFF4E8C8u, 0xFF140C04u, 1.3f * S);
    }
    // -- the ZOOM, only while it changes --
    if (zoomA > 0.0f && fo) {
        char zb[12]; sprintf(zb, "%.1fx", clampf(c.mmZoom, 1.0f, 24.0f));
        const float sz = snap(13.0f * S), w = snap(fo->measure(zb, sz) + 16.0f * S), h = snap(sz + 8.0f * S);
        const float yc = cy + rc - snap(34.0f * S);
        const u32 A = (u32)(zoomA * 255.0f);
        color_state(dev);
        rrect(dev, snap(cx - w * 0.5f), snap(yc - h * 0.5f), w, h, h * 0.5f, ((u32)(zoomA * 200.0f) << 24) | 0x00101418u, ((u32)(zoomA * 200.0f) << 24) | 0x00101418u);
        fo->begin(dev); fo->draw_c(dev, snap(cx), snap(yc), zb, sz, (A << 24) | 0x00F4EEDFu, (A << 24), 0.8f);
    }
    color_state(dev);
}

// ---- ASTRAL kit : the pieces' files and one full-canvas layer draw (the kit's 1024 canvas holds a ring of diameter
// 1000, so the quad is 1.024 x the ring diameter, centred) ----
static const char* astral_path(int i) {
    static const char* NAME[AS_N] = { "lentille_ombre", "anneau_base", "anneau_graduations", "anneau_reflet", "pastille_cardinal",
                                      "lettre_N", "lettre_E", "lettre_S", "lettre_W" };
    static char b[AS_N][260];
    if (!b[i][0]) { char rel[96]; _snprintf(rel, sizeof(rel), "assets\\astral\\laiton_%s.raw", NAME[i]); rel[95] = 0; plugin_path(b[i], 260, rel); }
    return b[i];
}
static const char* astral_path_grey(int i) {                    // the same pieces in grey : tinted by the theme's metal
    static const char* NAME[AS_N] = { "lentille_ombre", "anneau_base", "anneau_graduations", "anneau_reflet", "pastille_cardinal",
                                      "lettre_N", "lettre_E", "lettre_S", "lettre_W" };
    static char b[AS_N][260];
    if (!b[i][0]) { char rel[96]; _snprintf(rel, sizeof(rel), "assets\\astral\\gris_%s.raw", NAME[i]); rel[95] = 0; plugin_path(b[i], 260, rel); }
    return b[i];
}
// a ring layer of the kit drawn as an ANNULUS (tannulus) : kit radius t (canvas 1024, lens 420, ring edge 500) maps
// LINEARLY to screen so that 420 -> rc (the lens edge) and 500 -> r (the outer edge) -- the Bezel-width slider thickens
// the painted ring instead of scaling the whole picture. The lens shadow keeps the lens's own scale (rc / 420).
static void draw_astral_ring(u32 dev, u32 tex, float cx, float cy, float rc, float r, float t0, float t1, float ang, u32 tint = 0xFFFFFFFFu) {
    const float k = (r - rc) / 80.0f;
    tex_state(dev, tex);
    tannulus(dev, cx, cy, rc + (t0 - 420.0f) * k, rc + (t1 - 420.0f) * k, t0 / 1024.0f, t1 / 1024.0f, ang, tint);
    dSetTex(dev, 0, 0);
}
static void draw_astral_shadow(u32 dev, u32 tex, float cx, float cy, float rc) {
    tex_state(dev, tex);
    tannulus(dev, cx, cy, rc * (390.0f / 420.0f), rc * (424.0f / 420.0f), 390.0f / 1024.0f, 424.0f / 1024.0f, 0.0f, 0xFFFFFFFFu);
    dSetTex(dev, 0, 0);
}

// longue-vue BRASS BEZEL around the round map : a wide beveled gold ring + a specular glint + fixed cardinal
// marks (N up -- our map is north-up). rOuter = widget radius ; the map lens fills radius rOuter - bw inside it.
static void draw_brass_bezel(u32 dev, const Frame& f, float cx, float cy, float r, float bw, float S) {
    color_state(dev);
    const float ri = r - bw;
    const u32 edge = 0xFF3A2B10u, hi = 0xFFF3DA8Au, mid = 0xFFC79A3Au, lo = 0xFF7A5E22u, inr = 0xFF2A1E0Cu;
    #define MM_AADISC(RR, COL) { const float rr_ = (RR); rrect(dev, cx - rr_, cy - rr_, 2.0f * rr_, 2.0f * rr_, rr_, (COL), (COL), 1.2f); }
    MM_AADISC(r,                edge)   // dark outer rim
    MM_AADISC(r - snap(1.0f * S), hi)   // bright bevel highlight
    MM_AADISC(r - snap(2.8f * S), mid)  // main brass face
    MM_AADISC(ri + snap(2.2f * S), lo)  // inner bevel shadow
    MM_AADISC(ri + snap(0.8f * S), inr) // dark inner edge (against the lens)
    #undef MM_AADISC
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                                             // metallic glint (upper-left)
    disc_glow(dev, cx - r * 0.46f, cy - r * 0.52f, bw * 0.35f, 0x4CFFF4CCu, bw * 0.9f);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Font* fo = f.font;                                                                       // fixed engraved cardinals
    if (fo) {
        fo->begin(dev);
        const float rm = (r + ri) * 0.5f, csz = snap(16.0f * S * clampf(ui_config().mmCardSz, 0.5f, 2.0f));   // bigger, proportional to the (large) lens ; scaled by the Cardinal-size slider
        static const char* NM[4] = { "N", "E", "S", "W" };
        static const float dx[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, dy[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
        for (int i = 0; i < 4; ++i) {
            const float lx = cx + dx[i] * rm, ly = cy + dy[i] * rm;
            fo->draw_c(dev, snap(lx), snap(ly + 1.0f * S), NM[i], csz, 0x70FFF6CDu, 0, 0.0f);   // under-highlight
            fo->draw_c(dev, snap(lx), snap(ly),            NM[i], csz, 0xFF1C1404u, 0, 0.0f);   // engraved dark
        }
    }
}

// Map-load retry SCHEDULE. It plateaus, it never terminates : 3 next-frame attempts (a CreateTexture miss right
// after a zone-in recovers instantly, so no black flash), then 300 ms apart through the "data not ready" window,
// then one attempt every 15 s for as long as the zone lasts. The old form was a budget of 12 that STOPPED after
// ~2.7 s, which turned a DAT briefly held by AV / a busy disk into a black map for the entire visit. Staying in
// the slow lane costs one file open per 15 s -- nothing -- and is the difference between "black for 15 s" and
// "black until you zone again".
static const int      MAP_SLOW_AFTER = 12;      // attempts before dropping to the slow lane
static const unsigned MAP_SLOW_MS    = 15000;   // slow-lane spacing
// The SCHEDULE stays here -- three immediate attempts, then a fast lane, then a 15 s slow lane that never
// stops -- but the arming goes through retry_clock.h, which owns the 0-sentinel rule this used to spell out by
// hand (`if (retryAt == 0) retryAt = 1`). One implementation of "try again later" in the project, not five :
// the four hand-rolled copies were the audit's semantic-duplication finding of 2026-09-12, and the one in
// map_dat.cpp that HAD diverged blocked every retry past 24.8 days of uptime.
// The first attempts ask for 1 ms rather than 0 : with a 16 ms frame it is the same "next frame", and it keeps
// the stamp off the sentinel, which is the whole reason retry_arm exists.
static void map_retry_later(int& tries, unsigned& retryAt) {
    ++tries;
    retry_arm(retryAt, tries < 3 ? 1u : (tries < MAP_SLOW_AFTER ? 300u : MAP_SLOW_MS));
}

// the LOD state of one entity : open addressing on the id ; a slot not seen for 30 frames is reused
Minimap::LodSt* Minimap::lod_slot(unsigned id) {
    const unsigned N = sizeof(lod_) / sizeof(lod_[0]);
    unsigned h = (id * 2654435761u) & (N - 1), freeAt = N;
    for (unsigned k = 0; k < 16; ++k, h = (h + 1) & (N - 1)) {
        LodSt& e = lod_[h];
        if (e.id == id) { e.frame = lodFrame_; return &e; }
        const bool stale = (e.id == 0) || (lodFrame_ - e.frame > 30u);
        if (stale && freeAt == N) freeAt = h;
    }
    if (freeAt == N) return 0;                                    // a full neighbourhood : no memory, no harm (no easing)
    LodSt& e = lod_[freeAt]; e.id = id; e.a = 0.0f; e.frame = lodFrame_;
    return &e;
}

void Minimap::on_device_lost() { for (int i = 0; i < AS_N; ++i) { astG_[i] = 0; astGR_[i] = TexRetry{}; } s_shAtlas = 0; for (int i = 0; i < AS_N; ++i) s_shAst[i] = 0; mkAtlas_ = 0; mkAtlasR_ = TexRetry{}; for (int i = 0; i < AS_N; ++i) { ast_[i] = 0; astR_[i] = TexRetry{}; } mapTex_ = 0; mapFileId_ = 0; mkPlayer_ = 0; mkMob_ = 0; elemTex_ = 0; wxTex_ = 0; wxR_ = TexRetry{}; moonDialTex_ = 0; moonDialR_ = TexRetry{}; moonPhTex_ = 0; moonPhR_ = TexRetry{}; skyTex_ = 0; skyR_ = TexRetry{}; moonTex_ = 0; moonKey_ = -1; mkTries_ = 0; mkNextMs_ = 0; }   // FORGET handles (+ re-arm the retry budget)
void Minimap::dispose() {
    s_shAtlas = 0; for (int i = 0; i < AS_N; ++i) s_shAst[i] = 0;
    for (int i = 0; i < AS_N; ++i) { release_texture(astG_[i]); astG_[i] = 0; astGR_[i] = TexRetry{}; }
    release_texture(mkAtlas_); mkAtlas_ = 0; mkAtlasR_ = TexRetry{};
    for (int i = 0; i < AS_N; ++i) { release_texture(ast_[i]); ast_[i] = 0; astR_[i] = TexRetry{}; }
    release_texture(mapTex_); mapTex_ = 0; mapFileId_ = 0;
    release_texture(mkPlayer_); release_texture(mkMob_); release_texture(elemTex_); release_texture(moonTex_); if (wxTex_) release_texture(wxTex_); if (moonDialTex_) release_texture(moonDialTex_); if (moonPhTex_) release_texture(moonPhTex_); if (skyTex_) release_texture(skyTex_);
    mkPlayer_ = 0; mkMob_ = 0; elemTex_ = 0; wxTex_ = 0; wxR_ = TexRetry{}; moonDialTex_ = 0; moonDialR_ = TexRetry{}; moonPhTex_ = 0; moonPhR_ = TexRetry{}; skyTex_ = 0; skyR_ = TexRetry{}; moonTex_ = 0; moonKey_ = -1; mkTries_ = 0; mkNextMs_ = 0;
}

void Minimap::draw(const Frame& f) {
    ui_config().mmVisible = false;                              // cleared unless we render below (gates the wheel-zoom)
    if (!visible_ || !f.game) return;
    const GameState& g = *f.game;
    const UiConfig& c = ui_config();
    if (!c.mmShow || !g.inGame) return;                         // phase 1a : live only (needs a real map record)
    u32 dev = f.dev;
    // BOUNDED retry, not a one-shot : this used to latch after a single attempt, so one transient miss (the updater
    // replacing the asset, a device not ready right after a zone-in) meant NO markers on the minimap and NO element
    // icons in the clock header for the whole device generation. Each texture is retried on its own.
    //
    // ... AND THE BUDGET WAS 12 ATTEMPTS OVER 2.4 SECONDS, WHICH IS NOT A PLATEAU. Past that the feature was dead
    // for the device generation with NOT ONE LINE anywhere -- the exact shape the comment above says it exists to
    // prevent, and the audit of 2026-09-12 caught the contradiction (the other budgets in this project run ~27x
    // longer). A zone-in that takes more than two seconds to hand us a usable device was enough to lose the
    // markers for good. Now: a minute of trying, spaced, and it SAYS SO at both ends -- when it finally loads
    // (instrument the success path too) and when it gives up, with the paths, because "my minimap has no blips"
    // is otherwise indistinguishable from an empty zone.
    // Uses retry_clock.h rather than the hand-rolled sentinel this carried : retry_due() IS that 0-sentinel
    // check, and the project has one correct implementation of it on purpose.
    const bool icons = (c.mmMarkStyle == 1) && ensure_raw_tex_mip(dev, mkAtlas_, mkAtlasR_, MK_ATLAS_PATH(), 512, 256) != 0;
    s_shAtlas = mkAtlas_;                                      // lent to the Help sample (same device)   // the v3 icon atlas (gen_map_markers.py) ; the classic blips are the fallback
    if ((!mkPlayer_ || !mkMob_ || !elemTex_) && mkTries_ < MK_TRIES) {
        if (retry_due(mkNextMs_)) {
            retry_arm(mkNextMs_, mkTries_ < 4 ? 1u : MK_RETRY_MS);   // the first few immediately (a device just becoming ready), then spaced
            ++mkTries_;
            if (!mkPlayer_) mkPlayer_ = load_raw_texture_mip(dev, MK_PLAYER_PATH(), 64, 64);
            if (!mkMob_)    mkMob_    = load_raw_texture_mip(dev, MK_MOB_PATH(),    64, 64);
            if (!elemTex_)  elemTex_  = load_raw_texture_mip(dev, ELEM_ATLAS_PATH(), 256, 32);
            if (mkPlayer_ && mkMob_ && elemTex_) {
                if (mkTries_ > 1) windower::debug::log("minimap: marker + element textures loaded after %d attempt(s)", mkTries_);
                mkTries_ = 0;
            } else if (mkTries_ >= MK_TRIES) {
                windower::debug::log("minimap: gave up after %d attempts -- player=%d mob=%d elements=%d. No blips and no "
                                     "element icons until a device reset. Expected files : %s | %s | %s",
                                     mkTries_, mkPlayer_ ? 1 : 0, mkMob_ ? 1 : 0, elemTex_ ? 1 : 0,
                                     MK_PLAYER_PATH(), MK_MOB_PATH(), ELEM_ATLAS_PATH());
            }
        }
    }

    const float S = scale_ * clampf(c.mmScale, 0.5f, 2.0f);     // overall box / header scale
    const float mapD = snap(220.0f * S * clampf(c.mmMapSize, 0.5f, 1.6f));   // map DIAMETER -- independent of the box
    // COMPACT layout : two thin bands (clock / map info), each above or below the map ; the classic header is off
    // DIAL layout (2) : round + the Astral bezel -> no bands at all, the infos live in the four CORNERS the round dial
    // leaves empty in its square box (the least space there is) ; without the round ring it behaves as Compact
    const bool  dialAsk = (c.mmLayout == 2), dialRing = (c.mmShape == 1 && c.mmClock && c.mmBezel);
    const bool  dialSq = dialAsk && c.mmShape == 0;                // the SQUARE watch case : a brass frame, the subdials on its corners
    const bool  dial = dialAsk && (dialRing || dialSq);
    const bool  compact = (c.mmLayout == 1) || dial || (dialAsk && !dial);
    const float cbH = (compact && !dial) ? clock_band_h(c, S) : 0.0f, ibH = (compact && !dial) ? info_band_h(c, S) : 0.0f;
    const bool  cbTop = (c.mmClkBand == 0), ibTop = (c.mmInfoBand == 1);
    const float topH = (cbTop ? cbH : 0.0f) + (ibTop ? ibH : 0.0f);
    // the DIAL's real footprint (the box and the edit rect hug it) : on the sides and the top only what the corner subdials
    // overhang ; at the bottom the band, when it is shown. Mirrors the geometry of draw_dial_instrument / the square case.
    float dialSide = 0.0f, dialBot = 0.0f;
    if (dial) {
        const float rr0 = mapD * 0.5f, sr0 = snap(rr0 * TUNE("mm.dial_sub_k", 0.165f)), rim = sr0 + 2.4f * S;
        const float fi0 = snap(rr0 * 0.16f * clampf(c.mmBezelW, 0.5f, 2.0f));
        const float off0 = dialSq ? rr0 - fi0 * 0.5f : 0.7071f * (rr0 + sr0 + TUNE("mm.dial_sub_gap", 1.5f) * S);
        const bool cornersTop = c.mmGrid || (c.mmClock && (c.mmClkTime || c.mmClkDay));
        const bool bandOn = c.mmClock && (c.mmClkMoon || c.mmClkTime || c.mmClkDay);
        const float sub = off0 + rim - rr0;                                                    // a corner subdial past the ring / frame
        dialSide = snap((sub > 0.0f && (cornersTop || bandOn) ? sub : 0.0f) + 3.0f * S);
        if (bandOn) {
            const float bandH0 = snap(TUNE("mm.dial_cap", 12.5f) * S) + 12.0f * S;
            const float by0r = dialSq ? off0 + sr0 * 0.95f : rr0 - 1.0f * S;
            const float bandW = dialSq ? rr0 : off0 + sr0 * 0.8f + TUNE("mm.dial_fillet", 12.0f) * S + 3.0f * S;   // the band's half width
            if (bandW - rr0 > dialSide) dialSide = snap(bandW - rr0 + 2.0f * S);
            dialBot = snap(by0r + bandH0 + 2.5f * S - rr0);                                  // the band's foot past the ring
        }
        if (dialBot < dialSide) dialBot = dialSide;
    }
    const float botH = (cbTop ? 0.0f : cbH) + (ibTop ? 0.0f : ibH) + (dial ? dialBot - dialSide : 0.0f);   // the dial : the band's extra depth
    // compact = the economy of space : the map sits nearly edge to edge (1 px of air), the classic keeps its 5 px
    // ... except around the Astral ring, whose cardinal medallions stand 4 % of the radius outside it (kit radius 520 / 500)
    const bool  astralRing = (c.mmShape == 1 && c.mmClock && c.mmBezel);
    // (medallion half-size 0.128 r x the Cardinal-size slider, centred ~0.94 r out : what passes the ring's edge)
    const float medOut = mapD * 0.5f * (0.128f * clampf(c.mmCardSz, 0.5f, 2.0f) - 0.06f);
    // the subdials sit in the corners at 45 deg, touching the ring : the box must hold their far edge and their caption
    const float bm = dial ? dialSide
                   : compact ? (astralRing ? snap((medOut > 0.0f ? medOut : 0.0f) + 4.0f * S) : snap(3.0f * S)) : (c.mmClock ? snap(5.0f * S) : 0.0f);   // inner margin around the map (inside the themed box)
    const float hdrW = snap(210.0f * S);                        // the clock header's natural width
    const float Hh = compact ? 0.0f : clock_header_height(c, S);   // clock header height (enabled rows only ; 0 if off)
    const bool  hasClock = (c.mmClock != 0) || (compact && ibH > 0.0f) || dial;   // the box (themed chrome) is drawn at all
    const int   cpos = (hasClock && !compact) ? ((c.mmClockPos < 0 || c.mmClockPos > 3) ? 0 : c.mmClockPos) : -1;   // classic header placement (-1 = none)
    // header BAND width (the content is CENTRED in `hbw`) + the box layout.
    const float cw = (cpos >= 0) ? clock_header_width(f, g.vana, c, elemTex_, S) : 0.0f;   // widest enabled row (incl. the long FR day row)
    const float fc = bm + snap(5.0f * S);                       // OUTER frame clearance : keeps the content off the themed box border
    // TOP/BOTTOM : band = content + fc on EACH side (the frame is on both left AND right of the header).
    // LEFT/RIGHT : band = TIGHT to the content ; the outer clearance is added by hx, and the MAP side keeps only a
    //              small bm gap. (A symmetric band here shoved a big empty margin between the text and the map.)
    float hbw = hdrW;
    if (cpos == 0 || cpos == 1) { if (cw > snap(20.0f * S)) hbw = cw + 2.0f * fc; }
    else if (cpos == 2 || cpos == 3) { if (cw > snap(20.0f * S)) hbw = cw; }
    float Wbox, boxH;
    if (compact) {                                              // [top bands][bm][map][bm][bottom bands]
        Wbox = mapD + 2.0f * bm;
        boxH = topH + mapD + 2.0f * bm + botH;
    } else if (cpos == 0 || cpos == 1) {                               // TOP / BOTTOM : header stacked with the map
        const float needW = mapD + 2.0f * bm;                  // width the map + side margins want
        Wbox = (needW > hbw ? needW : hbw);                   // box width = max(header, map+margins)
        boxH = Hh + mapD + 2.0f * bm;                          // header + gap + map + margins
    } else if (cpos == 2 || cpos == 3) {                       // LEFT / RIGHT : [fc][content][bm gap][map][bm]
        Wbox = fc + hbw + bm + mapD + bm;                     // content clears the OUTER frame (fc) and hugs the map (small bm gap)
        boxH = (Hh > mapD ? Hh : mapD) + 2.0f * bm;            // box height = max(header, map) + margins
    } else {                                                   // no clock : the box is just the map
        Wbox = mapD;
        boxH = mapD;
    }
    // the clock box reserves the frame theme's border (0 = normal frames) : the box rect grows by it on each side, the
    // layout below (Wbox / boxH = the CONTENT area) sits that far inside
    const float bi = hasClock ? box_inset(c.mmBox) : 0.0f;
    const float Wout = Wbox + 2.0f * bi, Hout = boxH + 2.0f * bi;

    // ---- position : user fraction wins over the layout default (px,py = BOX top-left) ----
    float px, py;
    if (c.mmPosSet && f.screenW > 0.0f && f.screenH > 0.0f) { px = snap(c.mmX * f.screenW); py = snap(c.mmY * f.screenH); }
    else { px = snap(px_); py = snap(py_); }
    // ---- SMART ANCHOR : a size change (Size, Map size, bands, ring, Classic <-> Compact...) must not push the box off
    //      where the user put it. The rect they PLACED is (mmX, mmY, mmAW x mmAH) ; the third of the screen its centre
    //      sits in names the edge it hangs from (left / centre / right, top / centre / bottom), and the box is re-laid
    //      around that point at its CURRENT size. Then it is kept on screen. A box placed before this existed
    //      (mmAW = 0) just keeps its top-left. ----
    static bool s_awDirty = false;
    if (c.mmPosSet && (c.mmAW <= 0.0f || c.mmAH <= 0.0f)) {        // placed before the anchor existed : today's size is the placed one
        ui_config().mmAW = Wout; ui_config().mmAH = Hout; s_awDirty = true;
    }
    if (c.mmPosSet && c.mmAW > 0.0f && c.mmAH > 0.0f && f.screenW > 0.0f && f.screenH > 0.0f) {
        const float cx0 = (px + c.mmAW * 0.5f) / f.screenW, cy0 = (py + c.mmAH * 0.5f) / f.screenH;
        const float ax = cx0 < 0.3333f ? 0.0f : (cx0 > 0.6667f ? 1.0f : 0.5f);
        const float ay = cy0 < 0.3333f ? 0.0f : (cy0 > 0.6667f ? 1.0f : 0.5f);
        px = snap(px + (c.mmAW - Wout) * ax); py = snap(py + (c.mmAH - Hout) * ay);
    }
    if (f.screenW > 0.0f && f.screenH > 0.0f) {                // never off screen (a box bigger than the screen keeps its top-left)
        if (px + Wout > f.screenW) px = snap(f.screenW - Wout); if (px < 0.0f) px = 0.0f;
        if (py + Hout > f.screenH) py = snap(f.screenH - Hout); if (py < 0.0f) py = 0.0f;
    }

    // ---- EDIT MODE : drag the WHOLE box (header + map) ----
    int dch = 0, dcv = 0;
    const float preX = c.mmX, preY = c.mmY; const bool preSet = c.mmPosSet;
    if (c.editLayout &&
        edit_box_drag(g_mmEdit, EDITBOX_MINIMAP, f, px, py, Wout, Hout, ZPERM_HUB,
                      ui_config().mmPosSet, ui_config().mmX, ui_config().mmY, dch, dcv, ui_config().mmScale))
        edit_box_grid(dev, f, g_mmEdit, px, py, Wout, Hout, dch != 0, dcv != 0);   // highlight the centre axis when snapped
    if (c.mmX != preX || c.mmY != preY || c.mmPosSet != preSet) {   // the user just placed it : this size is the placed one
        ui_config().mmAW = Wout; ui_config().mmAH = Hout; s_awDirty = true;
    }
    if (s_awDirty && !g_mmEdit.dragging) { s_awDirty = false; save_ui_config(); }   // persist it once the drag is over

    // ---- Vana'diel clock box : the box background FOLLOWS the party/alliance box theme (like every other box),
    // with the clock header on top ; the map fills the region below. Drawn FIRST (behind the map). ----
    const float boxPy = py;
    ui_config().mmBoxX = px; ui_config().mmBoxY = py; ui_config().mmBoxW = Wout; ui_config().mmBoxH = Hout;   // the whole box, for the dev option sweep
    // ---- header top-left (hx,hy) and MAP CENTRE (ccx,ccy) depend on the clock placement (mmClockPos) ----
    const float ix = px + bi, iy = py + bi;                    // the content area's top-left (inside the frame border)
    const float r = mapD * 0.5f;
    float hx = ix, hy = iy, ccx, ccy;
    // compact : band rects (the clock outermost, the info band against the map)
    float cbY = 0.0f, ibY = 0.0f;
    if (compact) {
        ccx = ix + Wbox * 0.5f; ccy = iy + topH + bm + r;
        const float mapBot = iy + topH + bm + mapD + bm;
        if (cbTop) cbY = iy; else cbY = mapBot + (ibTop ? 0.0f : ibH);
        if (ibTop) ibY = iy + (cbTop ? cbH : 0.0f); else ibY = mapBot;
    } else if (cpos == 0) {                                          // TOP : header above the map
        hx = ix + (Wbox - hbw) * 0.5f;  hy = iy;
        ccx = ix + Wbox * 0.5f;         ccy = iy + Hh + bm + r;
    } else if (cpos == 1) {                                    // BOTTOM : header below the map
        ccx = ix + Wbox * 0.5f;         ccy = iy + bm + r;
        // subtract the header's intrinsic top lead (draw_clock_header starts at y+6S) so the map<->clock gap == bm
        // (the TOP reference) ; the freed 6S lands under the header, mirroring TOP's top pad.
        hx = ix + (Wbox - hbw) * 0.5f;  hy = iy + bm + mapD + bm - snap(6.0f * S);
    } else if (cpos == 2) {                                    // LEFT : [fc][content][bm gap][map][bm]
        hx = ix + fc;                   hy = iy + (boxH - Hh) * 0.5f;   // content clears the outer (left) frame by fc
        ccx = ix + fc + hbw + bm + r;   ccy = iy + boxH * 0.5f;         // map hugs the content : only a bm gap
    } else if (cpos == 3) {                                    // RIGHT : [bm][map][bm gap][content][fc]
        ccx = ix + bm + r;              ccy = iy + boxH * 0.5f;
        hx = ix + bm + mapD + bm;       hy = iy + (boxH - Hh) * 0.5f;   // content hugs the map (bm) ; fc clears the outer (right) frame
    } else {                                                   // no clock : the map fills the box
        ccx = ix + Wbox * 0.5f;         ccy = iy + boxH * 0.5f;
    }
    const float mapLeft = ccx - r, mapTop = ccy - r;           // map region top-left
    if (hasClock) {
        // rebuild the supersampled moon texture only when the day/phase changes (cheap otherwise).
        if (c.mmClkMoon) {
            const VanaClock mv = dial_vana(g.vana);                     // the dev test overrides reach the moon too
            const int di = (mv.dayIdx < 0 || mv.dayIdx > 7) ? 2 : mv.dayIdx;
            const int mkey = di * 1000 + mv.moonPct * 4 + (mv.waning ? 2 : 0) + (c.mmLayout == 2 ? 100000 : 0);
            if (mkey != moonKey_) {
                if (moonTex_) { release_texture(moonTex_); moonTex_ = 0; }
                static u32 moonBuf[64 * 64];
                build_moon_argb(moonBuf, 64, c.mmLayout == 2 ? 0xFFF0D898u : VANA_DAYCOL[di], mv.moonPct / 100.0f, mv.waning);   // the dial's moon is GILT, like a watch's
                moonTex_ = make_texture_argb_mip(dev, 64, 64, moonBuf);
                if (moonTex_) moonKey_ = mkey;   // latch the key ONLY on success : a failed bake (not-ready device) left moonKey_ committed while moonTex_=0, so the rebuild never re-entered and the moon was a blank hole for up to ~57 min (the Help path already guards this way)
            }
        }
        color_state(dev);
        draw_themed_box(dev, f.skin, px, boxPy, Wout, Hout, c.mmBox, 1.0f, S);   // shared themed chrome (Same-as-Party / own theme, transparency, border)
        if (compact) draw_compact_plate(dev, px, boxPy, Wout, Hout, ccx - r, ccy - r, mapD, c.mmShape == 1, cbY, cbH, ibY, ibH, S);   // + the bands' faces, their rules' rails and the map well
        if (!compact) draw_clock_header(dev, f, g.vana, c, elemTex_, moonTex_, hx, hy, hbw, S);
        else if (cbH > 0.0f) draw_clock_band(dev, f, g.vana, c, elemTex_, moonTex_, ix, cbY, Wbox, S);
    }
    const bool  round = (c.mmShape == 1);
    const float MSc = clampf(c.mmMarkerScale, 0.5f, 2.0f);

    // no map image chrome -> the map fills the region below the header.
    if (!g.map.valid) {                                        // zone has no map record -> header only
        if (f.font) { f.font->begin(dev); f.font->draw_c(dev, snap(ccx), snap(ccy), (c.lang == 1) ? "Pas de carte" : "No map", snap(14.0f * S), 0xFF8894A4u, 0xFF000000u, 1.4f); }
        return;
    }

    // ---- sync the map texture : (re)load from the ROM DAT when the zone's map file-id changes. If the DAT read
    //      FAILS (data not ready right after a zone-in, or a registry/path hiccup on some installs), RETRY a bounded
    //      number of times instead of caching the failure until the next zone -- that was the "minimap doesn't
    //      always load when I zone" bug. On success we stop retrying ; after the budget we give up (truly no map). ----
    if (g.map.fileId != mapFileId_) {                          // zone's map changed
        if (mapTex_) { release_texture(mapTex_); mapTex_ = 0; }
        mapFileId_ = g.map.fileId;
        mobAngN_ = 0;                                           // new zone -> drop the eased-angle cache
        mapTries_ = 0; mapRetryAt_ = 0;                        // new zone -> fresh schedule, try immediately
        s_mapFail.on = false;                                  // a finding about the zone we left is not about this one
    }
    if (mapTex_ == 0 && g.map.fileId) {                        // not loaded yet -> (re)try, throttled. NO terminating budget : see minimap.h
        if (retry_due(mapRetryAt_)) {   // retry_clock.h owns the 0-sentinel case (set on every zone change) : without it the map stays black past 24.8 d of uptime
            u32* pixels = 0; int mw = 0, mh = 0; MapLoadDiag md;
            // Only STOP retrying when the texture actually exists. This used to clear the budget as soon as the DAT
            // DECODED, without checking make_texture_argb_mip -- so a decode that succeeded followed by a failed
            // texture create left the map black forever with retries spent and NOTHING logged (the failure log lives
            // in the else branch). That is the black-map-on-a-valid-record case: file fine, texture missing.
            if (load_zone_map(g.map.fileId, pixels, mw, mh, &md)) {
                mapTex_ = make_texture_argb_mip(dev, mw, mh, pixels); mapW_ = mw; mapH_ = mh; free_map_image(pixels);
                if (mapTex_) {
                    s_mapFail.on = false;
                    // INSTRUMENT THE SUCCESS PATH (rule 10 corollary) : a recovery in the slow lane is the proof
                    // that the failure was transient. Without this line a fixed black map and a map that was never
                    // broken read exactly the same in the log.
                    if (mapTries_ >= MAP_SLOW_AFTER)
                        windower::debug::log("MAP OK zone=%u fileId=0x%04X : loaded on slow-lane attempt %d (~%d s after the zone-in) -- the earlier failure WAS transient",
                                             g.map.zone, g.map.fileId, mapTries_ + 1,
                                             (MAP_SLOW_AFTER * 300 + (mapTries_ - MAP_SLOW_AFTER + 1) * MAP_SLOW_MS) / 1000);
                    mapTries_ = 0;
                }
                else {
                    map_retry_later(mapTries_, mapRetryAt_);
                    if (mapTries_ == MAP_SLOW_AFTER) {
                        s_mapFail.on = true; s_mapFail.texFailed = true; s_mapFail.zone = g.map.zone; s_mapFail.fileId = g.map.fileId;
                        s_mapFail.W = mw; s_mapFail.H = mh;
                    }
                    if (mapTries_ == MAP_SLOW_AFTER)
                        windower::debug::log("MAP FAIL zone=%u fileId=0x%04X : DAT decoded fine (%dx%d) but CreateTexture failed -- image too large for the device, or out of video memory. Slowing to one attempt every %d s (NOT giving up)",
                                             g.map.zone, g.map.fileId, mw, mh, MAP_SLOW_MS / 1000);
                }
            }
            else {
                map_retry_later(mapTries_, mapRetryAt_);   // fast lane then a 15 s slow lane -- never a permanent stop
                // ALWAYS-ON failure log (not behind a command) : a black minimap is rare and unpredictable, so
                // a probe you must remember to arm would miss the occurrence. Fired ONCE, on the attempt that
                // drops into the slow lane -- by then it is a real failure, not the normal not-ready-yet right
                // after a zone-in, and `step` names WHICH stage failed (this is the line that answers "is it the
                // path, or a file briefly unreadable" -- PATH UNRESOLVED vs FILE UNREADABLE).
                if (mapTries_ == MAP_SLOW_AFTER) {
                    s_mapFail.on = true; s_mapFail.texFailed = false; s_mapFail.zone = g.map.zone; s_mapFail.fileId = g.map.fileId;
                    s_mapFail.step = md.step; s_mapFail.fileSize = md.fileSize; s_mapFail.W = md.W; s_mapFail.H = md.H;
                    s_mapFail.biSize = md.biSize; s_mapFail.bpp = md.bpp; s_mapFail.overlay = md.overlay;
                    static const char* STEP[8] = { "OK", "NO FFXI ROOT", "PATH UNRESOLVED", "FILE UNREADABLE", "NO GRAPHIC CHUNK", "FORMAT REJECTED", "?", "?" };
                    windower::debug::log("MAP FAIL zone=%u submap=%d valid=%d flags=0x%04X fileIdx=%u fileId=0x%04X scale=%d off=(%d,%d)",
                                         g.map.zone, current_submap(), g.map.valid ? 1 : 0, g.map.flags,
                                         g.map.fileIdx, g.map.fileId, g.map.scale, g.map.offX, g.map.offY);
                    windower::debug::log("MAP   step=%s  overlay=%d  size=%u  chunkTypes=0x%08X  dims=%dx%d  fmtFlags=0x%02X  biSize=%u bpp=%u",
                                         STEP[md.step & 7], md.overlay ? 1 : 0, md.fileSize, md.chunkTypes, md.W, md.H, md.fmtFlags, md.biSize, md.bpp);
                    windower::debug::log("MAP   path='%s'", md.path[0] ? md.path : "<unresolved>");
                }
            }
        }
    }

    // Don't draw the map PANEL until its texture exists. The clock header is already drawn above and stays --
    // only the map area waits. Otherwise the box appears as a black rectangle and fills in a moment later, which
    // is what "elle pop noir puis s'affiche" describes: the load can miss on the first frame after a zone-in and
    // only succeed on a retry. Once the budget is spent we fall through and show the normal empty panel, so a
    // zone whose map genuinely cannot load still renders its frame rather than silently vanishing.
    if (!mapTex_ && mapTries_ < MAP_SLOW_AFTER) return;   // only the FAST lane waits ; in the slow lane draw the normal empty panel and keep retrying underneath

    // BLACK-MAP catch-all. The block above only logs after its 12 retries are spent, and it only RUNS when
    // g.map.fileId is non-zero -- so a record that is "valid" with fileId 0 (or one whose retries were never
    // armed) left the map black with NOTHING in the log. That blind spot is exactly what the first capture hit.
    // One line per zone, only once the texture is genuinely absent and no retry budget is left.
    if (!mapTex_) {
        static unsigned lastLoggedZone = 0xFFFFFFFFu;
        if (g.map.zone != lastLoggedZone && mapTries_ >= MAP_SLOW_AFTER) {
            lastLoggedZone = g.map.zone;
            windower::debug::log("MAP BLACK zone=%u submap=%d valid=%d fileId=0x%04X fileIdx=%u flags=0x%04X scale=%d tries=%d",
                                 g.map.zone, current_submap(), g.map.valid ? 1 : 0, g.map.fileId, g.map.fileIdx,
                                 g.map.flags, g.map.scale, mapTries_);
            const char* key = 0; const char* rom = ffxi_rom_dir_probe(&key);
            windower::debug::log("MAP   ROM dir : %s   (registry key: %s)", rom ? rom : "<NOT FOUND>", key ? key : "<none>");
        }
    }

    // ---- player native map position (transform calibrated to the 512px native map) ----
    const float mapPx = 512.0f;
    const float mapX = (g.map.scale * g.meX) / 5.0f - (float)g.map.offX;
    const float mapY = (g.map.scale * -g.meZ) / 5.0f - (float)g.map.offY;

    // ---- wheel zoom (player-centred) : the mouse callback fills mmWheel when the cursor is over us. Skipped while
    // the config live-preview is driving our position (else the wheel-save would persist the TEMP stage position). ----
    if (!c.editLayout && !c.mmPreview && f.screenW > 0.0f && f.screenH > 0.0f) {   // expose our rect (screen fractions) for the callback
        ui_config().mmVisible = true;
        ui_config().mmHitX = mapLeft / f.screenW; ui_config().mmHitY = mapTop / f.screenH;
        ui_config().mmHitW = mapD    / f.screenW; ui_config().mmHitH = mapD / f.screenH;
    }
    if (c.mmWheel != 0 && !c.mmPreview) {
        ui_config().mmZoom = clampf(c.mmZoom * powf(1.18f, (float)c.mmWheel), 1.0f, 24.0f);   // exponential -> smooth over a wide range
        ui_config().mmWheel = 0; save_ui_config();
    }

    const float Z   = clampf(c.mmZoom, 1.0f, 24.0f);
    const float pn  = (mapD / mapPx) * Z;                     // screen px per native map px (Z=1 => full map spans the lens)

    if (dial && !dialSq && c.mmDayDial == 2) {                           // THE MODULES' SLOTS and THE COLLAR (one cast piece for all)
        s_dayTh = POD_SLOT[(c.mmPodDay < 0 || c.mmPodDay > 5) ? 3 : c.mmPodDay];
        s_moonTh = POD_SLOT[(c.mmPodMoon < 0 || c.mmPodMoon > 5) ? 2 : c.mmPodMoon];
        s_hourTh = c.mmPodHP == 1 ? 1.5707963f : -1.5707963f; s_posTh = -s_hourTh;
        s_skyTh = POD_SLOT[(c.mmPodSky < 0 || c.mmPodSky > 5) ? 5 : c.mmPodSky];
        s_skyPod = c.mmClock && (c.mmClkTime || c.mmClkDay) && TUNE("mm.foot_body", 0.0f) <= 0.5f;
        s_podN = 0;
        if (TUNE("mm.foot_body", 0.0f) <= 0.5f) {
            dial_metal_set(c.mmFrame != 0 ? mm_frame_color(c, f) : 0u);
            const PodGeo G = pod_geo(r, S);
            const float D = r * TUNE("mm.pod_depth", 0.235f), Ri = r - 3.0f * S, Ro = r + D, rmT = (Ri + Ro) * 0.5f, rwT = r + D * 0.5f;
            Font* tfP = band_font(f, MM_TIME);
            const float pSzP = snap(TUNE("mm.dial_cap", 12.5f) * S);
            auto add = [&](float th, float ph) { if (s_podN < POD_MAX) { s_podTh[s_podN] = th; s_podPh[s_podN] = ph; ++s_podN; } };
            if (c.mmClock && c.mmClkDay) add(s_dayTh, G.Ph);
            if (c.mmClock && c.mmClkMoon) add(s_moonTh, G.Ph);
            if (s_skyPod) add(s_skyTh, sky_pod_ph(r, S));
            if (c.mmClock && c.mmClkTime && tfP) add(s_hourTh, (snap(drum_width(tfP, "00:00", pSzP, S) * 0.5f + 3.0f * S) + 3.0f * S) / rwT + 2.0f * S / rmT);
            if (c.mmGrid && tfP) {
                const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
                if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) {
                    char pt[12]; sprintf(pt, "%c-%d", 'A' + gc, gr + 1);
                    add(s_posTh, (snap(drum_width(tfP, pt, pSzP, S) * 0.5f + 3.0f * S) + 3.0f * S) / rwT + 2.0f * S / rmT);
                }
            }
            pod_collar(dev, ccx, ccy, r, S, dm_toward(g_dm.bot, 0xFF000000u, TUNE("mm.pod_dark", 0.25f)));
        }
    }
    if (dial && !dialSq && c.mmDayDial == 2 && s_skyPod) {               // the SKY MODULE : the day/night window + the weather's enamel wheels
        const VanaClock sv = dial_vana(g.vana);
        ensure_raw_tex_mip(dev, wxTex_, wxR_, plugin_path_r("assets\\weather_icons.raw"), 1024, 128);
        draw_sky_pod(dev, ccx, ccy, r, S, s_skyTh, sv.hh, sv.mm, mm_weather(g.weather), wxTex_,   // (the painted scenes : off -- the code's sky is easier to frame, the user 2026-11-25)
                     TUNE("mm.sky_tex", 0.0f) > 0.5f ? ensure_raw_tex_mip(dev, skyTex_, skyR_, plugin_path_r("assets\\sky_frames.raw"), 1024, 512) : 0u);
    }
    if (dial && !dialSq && c.mmDayDial == 2 && c.mmClock && c.mmClkDay) {          // the DAY DISC : under the frame, the map and the ring -- and BEHIND the position's tube (drawn before it)
        dial_metal_set(c.mmFrame != 0 ? mm_frame_color(c, f) : 0u);
        const VanaClock dv0 = dial_vana(g.vana);
        const int di0 = (dv0.dayIdx < 0 || dv0.dayIdx > 7) ? 2 : dv0.dayIdx;
        ensure_raw_tex_mip(dev, wxTex_, wxR_, plugin_path_r("assets\\weather_icons.raw"), 1024, 128);
        draw_day_disc(dev, ccx, ccy, r, S, di0, ((float)dv0.hh * 60.0f + (float)dv0.mm) / 1440.0f, wxTex_, mm_weather_elem(mm_weather(g.weather)));
    }
    if (dial) {                                                          // the MODULES : behind the compass (drawn before its frame, map and ring)
        dial_metal_set(c.mmFrame != 0 ? mm_frame_color(c, f) : 0u);
        ensure_raw_tex_mip(dev, wxTex_, wxR_, plugin_path_r("assets\\weather_icons.raw"), 1024, 128);
        PERF_SCOPE("mm.modules");
        // the inner radius as the frame / ring below will set it (the astral ring : 80/500 of r ; the square case : its inset)
        const float rcM = dialSq ? r - snap(r * 0.16f * clampf(c.mmBezelW, 0.5f, 2.0f)) : r - r * (80.0f / 500.0f) * clampf(c.mmBezelW, 0.5f, 2.0f);
        s_moonDialTex = ensure_raw_tex_mip(dev, moonDialTex_, moonDialR_, plugin_path_r("assets\\moon_dial.raw"), 512, 512);
        ensure_raw_tex_mip(dev, moonPhTex_, moonPhR_, plugin_path_r("assets\\moon_phases.raw"), 1024, 128);
        if (!dialSq && c.mmDayDial == 2 && c.mmClock && c.mmClkMoon) {    // the MOON POD : the disc behind the compass, three phases in a window
            const VanaClock mv = dial_vana(g.vana);
            const float pct = (float)mv.moonPct / 100.0f, cyc = mv.waning ? 0.5f + (1.0f - pct) * 0.5f : pct * 0.5f;
            draw_moon_pod(dev, ccx, ccy, r, S, mv.moonPct, mv.waning, mv.hh, moonPhTex_); (void)cyc;   // the exact phase, hour by hour
        }
        draw_dial_instrument(dev, f, c, g, dial_vana(g.vana), elemTex_, wxTex_, mapX, mapY, ccx, ccy, rcM, r, S, 0.0f, dialSq, 0);
    }
    if (false && dial && !dialSq && c.mmDayDial == 2 && c.mmGrid) {       // (the position plate : replaced by the band's centre compartment)
        dial_metal_set(c.mmFrame != 0 ? mm_frame_color(c, f) : 0u);
        const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
        if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) { char gb[12]; sprintf(gb, "%c-%d", 'A' + gc, gr + 1); draw_pos_pod(dev, band_font(f, MM_TIME), ccx, ccy, r, S, gb); }
    }
    PERF_BEGIN("mm.frame");
    // ---- box-theme frame : draw the REAL box-theme window chrome (the same 9-slice / procedural border the
    // party/target/player boxes use) and inset the map INSIDE its border -- so "Box Theme" is the theme's actual
    // frame, not just its colour approximated by a stroke. Square only : the chrome is rectangular, so a round
    // minimap keeps a themed-COLOUR ring (drawn below). We follow the shared party/alliance box theme (skinTheme).
    bool  themedOk = false;
    float fi = 0.0f;                                          // frame inset : keep the map clear of the chrome border
    dial_metal_set(c.mmFrame != 0 ? mm_frame_color(c, f) : 0u);   // the dial's metal : the theme / custom colour / the brass
    if (dialSq) {                                                  // the square watch case (the map is drawn inside it)
        fi = snap(r * 0.16f * clampf(c.mmBezelW, 0.5f, 2.0f));
        draw_square_case(dev, ccx, ccy, r, fi, S);
    } else if (c.mmFrame == 1 && !round) {
        const int theme = c.skinTheme; const float lum = c.skinLum;
        if (window_theme_is_proc(theme)) { draw_proc_window(dev, theme, mapLeft, mapTop, mapD, mapD, 0xFFFFFFFFu, false, true, lum, c.skinHue); fi = snap(6.0f * S); themedOk = true; }
        else if (f.skin && f.skin->ready()) { draw_window(dev, *f.skin, mapLeft, mapTop, mapD, mapD, 0xFFFFFFFFu, S, false, true); fi = snap(15.0f * S); themedOk = true; }
        if (themedOk) fi += frame_inset();                    // a frame theme's thicker border : the map clears it too (0 = normal frames)
    }
    const float wx0 = mapLeft + fi, wy0 = mapTop + fi, wx1 = mapLeft + mapD - fi, wy1 = mapTop + mapD - fi;   // clip rect (inset past the chrome border)

    // ---- ROUND border : the 9-slice chrome can't wrap a circle, so render the theme's (or custom) border colour
    // as a crisp solid ring -- a thin dark AA outline + the colour band -- and inset the circular clip so the map
    // fills only the interior, leaving the ring visible. Gives the round minimap a real border, not a soft glow. ----
    float rc = r;                                            // circle clip radius = the interior map area (inside the ring)
    // ASTRAL : the textured compass kit (assets/astral, scripts/gen_astral.py) when every piece is loaded ; the vector
    // brass bezel below stays the fallback (a piece that will not load never leaves the map without a ring)
    bool astral = false;
    if (round && c.mmClock && c.mmBezel) {
        astral = true;
        for (int i = 0; i < AS_N; ++i) if (!ensure_raw_tex_mip(dev, ast_[i], astR_[i], astral_path(i), AS_SZ[i], AS_SZ[i])) astral = false;
        if (c.mmFrame != 0) for (int i = 0; i < AS_N; ++i) ensure_raw_tex_mip(dev, astG_[i], astGR_[i], astral_path_grey(i), AS_SZ[i], AS_SZ[i]);
    }
    if (astral) for (int i = 0; i < AS_N; ++i) s_shAst[i] = ast_[i];   // lent to the Help sample
    if (astral) {
        rc = r - r * (80.0f / 500.0f) * clampf(c.mmBezelW, 0.5f, 2.0f);   // the kit's ring is 80 / 500 of the radius (manifest.json) x the Bezel-width slider
    } else if (round && c.mmClock && c.mmBezel) {
        // longue-vue INSTRUMENT look (the Vana box) : a wide brass bezel + fixed cardinals around the lens.
        const float bezelW = snap(22.0f * S * clampf(c.mmBezelW, 0.5f, 2.0f));   // Bezel-width slider
        rc = r - bezelW;
        draw_brass_bezel(dev, f, ccx, ccy, r, bezelW, S);
    } else if (round && c.mmClock && !c.mmBezel) {
        // no circle at all : no bezel, no cardinals -> the map lens fills the full radius (rc stays r).
    } else if (round && c.mmFrame != 0) {
        const bool procThemed = (c.mmFrame == 1 && window_theme_is_proc(c.skinTheme));   // real procedural frame available
        const float ringT = snap((procThemed ? 7.0f : 5.0f) * S);   // proc frame needs a thicker band to read the bevel/filet
        rc = r - ringT;
        if (procThemed) {
            // the EXACT theme border, rendered as a circular ring (gold plate + filet for Royal, iron/steel
            // bevel, neon tube, frost rime) -- matches the box frame, not a flat colour.
            draw_box_border_ring(dev, c.skinTheme, ccx, ccy, r, rc, c.skinLum, c.skinHue);
        } else {
            // FFXI skin border colour OR a custom colour : a clean AA ring (dark rim + colour band).
            const u32 fc = mm_frame_color(c, f);
            color_state(dev);
            const u32 rim = mm_darken(fc, 0.55f);
            rrect(dev, ccx - r, ccy - r, 2.0f * r, 2.0f * r, r, rim, rim, snap(1.2f * S));               // AA dark outer rim
            const float ri = r - snap(1.0f * S);
            rrect(dev, ccx - ri, ccy - ri, 2.0f * ri, 2.0f * ri, ri, fc, fc, snap(1.2f * S));            // AA colour band
        }
    }

    // ---- optional backdrop behind the map (mmBgAlpha ; 0 = transparent so the game shows through). Skipped
    // under a themed square frame -- the chrome already paints its own interior behind the map. ----
    if (!themedOk && c.mmBgAlpha > 0.004f) {
        color_state(dev);
        const u32 bg = ((u32)(clampf(c.mmBgAlpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) | 0x00080B12u;
        if (round) disc(dev, ccx, ccy, rc, bg); else grad_quad(dev, wx0, wy0, wx1 - wx0, wy1 - wy0, bg, bg, bg, bg);
    }

    PERF_END("mm.frame");
    PERF_BEGIN("mm.map");
    // ---- the map image, player-centred, V-flipped to read upright. SQUARE = a rect-clamped quad ; ROUND = a
    // textured circular FAN (`tdisc`) so the map is genuinely round with NO stencil buffer -- the round shape is
    // entirely geometry (fan + circle-tested markers), no clip mask needed. ----
    if (mapTex_) {
        const float qx = ccx - mapX * pn, qy = ccy - mapY * pn, qs = mapPx * pn;   // native (0,0)..(512,512)
        tex_state(dev, mapTex_);
        if (round) {
            const float u0c = mapX / mapPx, v0c = 1.0f - mapY / mapPx;             // player UV (V flipped) at the centre
            tdisc(dev, ccx, ccy, rc, u0c, v0c, 1.0f / qs, -1.0f / qs, 0xFFFFFFFFu);
            if (astral) draw_astral_shadow(dev, ast_[AS_SHADOW], ccx, ccy, rc);   // the lens shadow : on the map, under the markers
        } else {
            const float cx0 = qx > wx0 ? qx : wx0, cy0 = qy > wy0 ? qy : wy0;
            const float cx1 = (qx + qs) < wx1 ? (qx + qs) : wx1, cy1 = (qy + qs) < wy1 ? (qy + qs) : wy1;
            if (cx1 > cx0 && cy1 > cy0) {
                const float u0 = (cx0 - qx) / qs, u1 = (cx1 - qx) / qs;            // U normal
                const float v0 = 1.0f - (cy0 - qy) / qs, v1 = 1.0f - (cy1 - qy) / qs;  // V flipped
                tquad(dev, snap(cx0), snap(cy0), snap(cx1) - snap(cx0), snap(cy1) - snap(cy0), u0, u1, v0, v1, 0xFFFFFFFFu, 0xFFFFFFFFu);
            }
        }
        dSetTex(dev, 0, 0);
    }

    PERF_END("mm.map");
    PERF_BEGIN("mm.lines");
    // ---- range ring : a circle at the configured game-distance (yalms) around you -- a pull / aggro gauge.
    //      Radius follows the map calibration (scale/5 native px per yalm) so it stays true under any zoom.
    //      Skipped when it would spill past the lens (lower the radius to bring it into view). ----
    if (c.mmRing && g.map.scale != 0) {
        const float ringPx = clampf(c.mmRingR, 3.0f, 50.0f) * ((float)g.map.scale / 5.0f) * pn;
        const float lim = (round ? rc : (mapD * 0.5f - fi));
        if (ringPx > 4.0f && ringPx <= lim) {
            color_state(dev);
            const u32 rgc = c.mmRingCol | 0xFF000000u, rim = mm_darken(rgc, 0.5f);
            rrect_stroke(dev, ccx - ringPx, ccy - ringPx, 2.0f * ringPx, 2.0f * ringPx, ringPx, rim, snap(3.0f * S));   // dark halo (readable over any map)
            rrect_stroke(dev, ccx - ringPx, ccy - ringPx, 2.0f * ringPx, 2.0f * ringPx, ringPx, rgc, snap(1.6f * S));   // colour ring
        }
    }

    // ---- target line : a line from you to your current <t> target, clamped to the lens edge when the target
    //      is off-map (so it still points the right way). Uses the target entity's world pos (g.target). ----
    if (c.mmTgtLine && g.target.valid && g.map.scale != 0) {
        const float ex = ((float)g.map.scale * g.target.posX) / 5.0f - (float)g.map.offX;
        const float ey = ((float)g.map.scale * -g.target.posZ) / 5.0f - (float)g.map.offY;
        float sx = ccx + (ex - mapX) * pn, sy = ccy + (ey - mapY) * pn;
        const float dx = sx - ccx, dy = sy - ccy, d = sqrtf(dx * dx + dy * dy);
        const float lim = (round ? rc : (mapD * 0.5f - fi)) - snap(2.0f * S);
        if (d > lim && d > 0.001f) { const float k = lim / d; sx = ccx + dx * k; sy = ccy + dy * k; }   // clamp to the lens edge -> points toward an off-map target
        {
            color_state(dev);
            // colour the line + reticle like the TARGET'S OWN MARKER (its dot colour : claim/allegiance), so it
            // matches the mob/PC pip. Fall back to the configured line colour if the target isn't a mapped entity.
            u32 lc = c.mmTgtLineCol | 0xFF000000u;
            for (int e = 0; e < g.mapEntN; ++e) if (g.mapEnts[e].id == g.target.id) { lc = mm_ent_color(g.mapEnts[e]) | 0xFF000000u; break; }
            const float w = clampf(2.8f * S, 2.4f, 6.0f);                   // readable at any UI scale (was ~2px -> too thin)
            seg_soft(dev, ccx, ccy, sx, sy, w + snap(2.6f * S), 0x88000000u);   // dark under-stroke (halo)
            seg_soft(dev, ccx, ccy, sx, sy, w, lc);                            // colour line
            // target-end RETICLE : a ring (+ centre pip) on the target so it reads even when the line is short.
            if (icons) {                                                  // the reticle icon : its fill in the claim colour, its gold frame
                const float ts = snap(17.0f * S * MSc);
                tex_state(dev, mkAtlas_);
                mk_cell(dev, MK_TARGET_FILL, sx, sy, ts, 0.0f, lc);
                mk_cell(dev, MK_TARGET_FRAME, sx, sy, ts, 0.0f, 0xFFFFFFFFu);
                dSetTex(dev, 0, 0);
            } else {
            const float rr = clampf(5.0f * S, 4.0f, 10.0f);
            rrect_stroke(dev, sx - rr, sy - rr, 2.0f * rr, 2.0f * rr, rr, 0x88000000u, snap(3.0f * S));   // dark rim
            rrect_stroke(dev, sx - rr, sy - rr, 2.0f * rr, 2.0f * rr, rr, lc, snap(1.8f * S));            // colour ring
            disc(dev, snap(sx), snap(sy), snap(1.6f * S), lc);                                            // centre pip
            }
            if (dial && c.mmInfoDist && f.font) {                         // the DIAL : the distance ON the target, where the eye already is
                char db[16]; const float dx = g.target.posX - g.meX, dz = g.target.posZ - g.meZ; fmt_yalms(db, sqrtf(dx * dx + dz * dz));
                const float ls = snap(TUNE("mm.dial_dist", 10.0f) * S), lw = f.font->measure(db, ls);
                float lx = sx + snap(11.0f * S), ly = sy - snap(9.0f * S);
                if (lx + lw > ccx + rc - 2.0f) lx = sx - snap(11.0f * S) - lw;   // flip to the left near the right rim
                color_state(dev);
                rrect(dev, snap(lx - 3.0f * S), snap(ly - ls * 0.6f), snap(lw + 6.0f * S), snap(ls * 1.2f), snap(3.0f * S), 0x90101418u, 0x90101418u);
                f.font->begin(dev); f.font->draw_lc(dev, snap(lx), snap(ly), db, ls, lc, 0xFF000000u, 0.6f);
            }
        }
    }

    // ---- Wide Scan TRACK : the native compass's arrow (that compass sits on the log window, which the chat park
    //      takes off screen). The server streams 0x0F5 ~2 a second while a track runs : a reply older than 3 s means
    //      the track is gone even when no "ended" status came. A pulsing ring on the target, a thin line to it ;
    //      off the lens the mark rides the rim, so it still points the way. ----
    if (c.mmScan && g.map.scale != 0) {
        const PartyState::ScanTrack& st = party().scan_track();
        if (st.ms && (unsigned)(model_now_ms() - st.ms) < 3000u) {
            const float ex = ((float)g.map.scale * st.x) / 5.0f - (float)g.map.offX;
            const float ey = ((float)g.map.scale * -st.z) / 5.0f - (float)g.map.offY;
            float sx = ccx + (ex - mapX) * pn, sy = ccy + (ey - mapY) * pn;
            const float dx = sx - ccx, dy = sy - ccy, d = sqrtf(dx * dx + dy * dy);
            const float lim = (round ? rc : (mapD * 0.5f - fi)) - snap(7.0f * S);
            const bool off = d > lim && d > 0.001f;
            if (off) { const float k = lim / d; sx = ccx + dx * k; sy = ccy + dy * k; }
            const u32 sc = c.mmScanCol | 0xFF000000u;
            color_state(dev);
            seg_soft(dev, ccx, ccy, sx, sy, snap(1.4f * S) + snap(2.0f * S), 0x55000000u);
            seg_soft(dev, ccx, ccy, sx, sy, snap(1.4f * S), (sc & 0x00FFFFFFu) | 0x99000000u);
            const float ph = f.t * 2.2f - floorf(f.t * 2.2f);                 // 0..1 pulse
            const float r0 = clampf(4.5f * S, 3.5f, 9.0f), rp = r0 + ph * r0 * 1.6f;
            const u32 pa = (u32)((1.0f - ph) * 200.0f) << 24;
            rrect_stroke(dev, sx - rp, sy - rp, 2.0f * rp, 2.0f * rp, rp, (sc & 0x00FFFFFFu) | pa, snap(1.6f * S));   // the expanding pulse
            rrect_stroke(dev, sx - r0, sy - r0, 2.0f * r0, 2.0f * r0, r0, 0x99000000u, snap(3.2f * S));
            rrect_stroke(dev, sx - r0, sy - r0, 2.0f * r0, 2.0f * r0, r0, sc, snap(1.8f * S));
            disc(dev, snap(sx), snap(sy), snap((off ? 1.2f : 2.0f) * S), sc);
            if (dial && c.mmInfoDist && f.font) {                         // the DIAL : its distance on the mark
                char db[16]; const float dx = st.x - g.meX, dz = st.z - g.meZ; fmt_yalms(db, sqrtf(dx * dx + dz * dz));
                const float ls = snap(TUNE("mm.dial_dist", 10.0f) * S), lw = f.font->measure(db, ls);
                float lx = sx + snap(10.0f * S), ly = sy + snap(9.0f * S);
                if (lx + lw > ccx + rc - 2.0f) lx = sx - snap(10.0f * S) - lw;
                color_state(dev);
                rrect(dev, snap(lx - 3.0f * S), snap(ly - ls * 0.6f), snap(lw + 6.0f * S), snap(ls * 1.2f), snap(3.0f * S), 0x90101418u, 0x90101418u);
                f.font->begin(dev); f.font->draw_lc(dev, snap(lx), snap(ly), db, ls, sc, 0xFF000000u, 0.6f);
            }
        }
    }

    // ---- clip the entity blips to the map so a blip crossing the rim is CUT at the frame instead of popping off
    //      whole. NB the stencil mask (rrect_clip) is NOT used here : Windower renders the overlay with no
    //      depth-stencil bound, so stencil ops no-op even when the device was created with one -> the mask never
    //      clips.
    //      CORRECTION 2026-09-12 : that sentence is FALSE as written, and it cost a regression. The audit of
    //      that day trusted it, removed the rounded stencil mask in gfx/draw.cpp as dead weight, and the
    //      HP/MP/TP bars went square-ended in game within the minute (reverted the same evening). The stencil
    //      DOES work ; what it does not do is govern pixels OUTSIDE the rect its mask pass cleared -- see the
    //      note over rrect_clip_begin in gfx/draw.h. Whatever was measured here, it was not "ops are ignored".
    //      Instead :
    //        SQUARE -> a sub-VIEWPORT is a hard rasterizer scissor (works with no stencil, clips even XYZRHW).
    //                  Blips may cross the rim ; the viewport cuts the overhang -> true slide-out.
    //        ROUND  -> no rectangle can be a circle, so keep the WHOLE blip inside the disc (it stops AT the rim,
    //                  no overhang). A true round slide would need a working stencil, which we don't have here.
    //      Only the mob + NPC/PC loops are wrapped ; the target-line reticle deliberately stays on the edge. ----
    const bool clipBlips = g.mapEntN > 0 && (c.mmMob || c.mmNPC || c.mmPC);
    D3DVIEWPORT8 vpSave; bool vpSet = false;
    if (clipBlips && !round && dGetViewport(dev, vpSave)) {
        long L = (long)wx0, T = (long)wy0, R = (long)wx1 + 1, B = (long)wy1 + 1;   // round the clip rect OUT by <=1px
        const long vl = (long)vpSave.X, vt = (long)vpSave.Y, vr = vl + (long)vpSave.Width, vb = vt + (long)vpSave.Height;
        if (L < vl) L = vl; if (T < vt) T = vt; if (R > vr) R = vr; if (B > vb) B = vb;   // never exceed the saved viewport
        if (R > L && B > T) { D3DVIEWPORT8 vp = vpSave; vp.X = (u32)L; vp.Y = (u32)T; vp.Width = (u32)(R - L); vp.Height = (u32)(B - T); dSetViewport(dev, vp); vpSet = true; }
    }
    const bool clipped = vpSet;   // only the square viewport clip lets a blip cross the rim ; round keeps blips wholly inside

    PERF_END("mm.lines");
    PERF_BEGIN("mm.markers");
    // ---- ICONS with a LEVEL OF DETAIL (the v3 atlas). A detailed icon needs ~12-16 px to read ; zoomed out, many
    //      entities share those pixels and the icons bury each other (a trust under a mob). So :
    //      * the measure is SCREEN PIXELS PER YALM (ppy), not the zoom -- every zone's map has its own scale ;
    //      * each entity is a DOT far away and its ICON close up, cross-faded over a band [P1, P2] x its class factor
    //        (the party turns into icons first, other players last ; plain NPCs stay dots, as the user chose) ;
    //      * a placement pass in PRIORITY order turns an icon that would cover a more important one back into a dot ;
    //      * the LANDMARKS (Home Point, Survival Guide, Waypoint, Moogle, ???) are always big icons with a halo.
    //      Every threshold and size is a TUNE knob (data\tune.txt, dev build) -- tuned in game, then set as default. ----
    s_groupN = 0;                                              // never a stale group from an earlier frame (no entities this one)
    if (icons && g.mapEntN > 0) {
        const float ppy =((float)g.map.scale / 5.0f) * pn;       // screen px per yalm
        ui_config().mmPpy = ppy;
        const float P1 = TUNE("mm.lod_p1", 0.5f), P2 = TUNE("mm.lod_p2", 1.1f);
        const float iconK = TUNE("mm.icon_k", 1.0f) * S * MSc, dotK = TUNE("mm.dot_k", 1.0f) * S * MSc;
        const float lmK = TUNE("mm.landmark_k", 1.0f) * S * MSc;
        struct Mk { float sx, sy, sz, ang, a, dr, prev; int fill, frame, prio; u32 col, frameCol, dotCol; bool ring; LodSt* st; };
        ++lodFrame_;
        float ldt = f.t - lodT_; lodT_ = f.t; if (ldt < 0.0f || ldt > 0.1f) ldt = 0.016f;   // frame time (f.t wraps ; a long gap = one step)
        const float ease = clampf(ldt * TUNE("mm.morph_speed", 9.0f), 0.0f, 1.0f);           // ~0.2-0.3 s for a full dot <-> icon morph
        static Mk mk[MAP_ENT_MAX];
        int n = 0;
        for (int e = 0; e < g.mapEntN && n < MAP_ENT_MAX; ++e) {
            const MapEntity& me = g.mapEnts[e];
            if (me.id && me.id == party().selfId_) continue;
            Mk m; m.ang = 0.0f; m.fill = -1; m.frame = -1; m.col = 0xFFFFFFFFu; m.frameCol = 0xFFFFFFFFu; m.ring = false;
            MkClass k;
            if (!mk_classify(me, c, k)) continue;
            m.prio = k.prio; m.fill = k.fill; m.frame = k.frame; m.col = k.col; m.frameCol = k.frameCol; m.ring = k.ring;
            const float cf = k.cf, isz = k.isz, dr = k.dr;
            if (k.dir) m.ang = eased_angle(me.id, me.heading + 1.5708f);
            // the fade : 0 = dot, 1 = icon
            if (cf < 0.0f) m.a = 0.0f;
            else if (cf == 0.0f) m.a = 1.0f;
            else {                                                   // ONE threshold (no frozen half-state) with a +-10 % hysteresis :
                const float T = (P1 + P2) * 0.5f * cf;                // the animation (below) makes the switch smooth, the margin
                const LodSt* st0 = me.id ? lod_slot(me.id) : 0;       // keeps a zoom right on the line from flipping back and forth
                const bool was = st0 && st0->a >= 0.5f;
                m.a = (ppy >= T * (was ? 0.9f : 1.1f)) ? 1.0f : 0.0f;
            }
            // a landmark shrinks a little when the map is far out (it must stay seen, not cover the player) ; full size from ~1 px/yalm
            m.sz = isz * (m.prio == 9 ? lmK * clampf(TUNE("mm.lm_far", 0.62f) + 0.4f * ppy, TUNE("mm.lm_far", 0.62f), 1.0f) : iconK);
            m.dr = dr * dotK * clampf(0.85f + 0.06f * ppy, 0.85f, 1.3f);
            m.dotCol = m.col;
            m.st = me.id ? lod_slot(me.id) : 0;
            m.prev = m.st ? m.st->a : m.a;
            const float ex = (g.map.scale * me.x) / 5.0f - (float)g.map.offX;
            const float ey = (g.map.scale * -me.z) / 5.0f - (float)g.map.offY;
            m.sx = ccx + (ex - mapX) * pn; m.sy = ccy + (ey - mapY) * pn;
            // a TRUST at your side says nothing (it follows you, or warps back when left behind) and its dots covered the
            // map around you : it is drawn only once it is AWAY from you (in TUNE("mm.trust_hide") yalms -- 12 by default : they trail you at 6-10)
            if (me.kind == MAPK_TRUST) {
                const float dxw = me.x - g.meX, dzw = me.z - g.meZ, hideY = TUNE("mm.trust_hide", 12.0f);
                if (dxw * dxw + dzw * dzw < hideY * hideY) continue;
            }
            const float half = (m.a > 0.0f ? m.sz * 0.5f : m.dr + 1.0f), pad = clipped ? half : -half;
            if (round) { const float dx = m.sx - ccx, dy = m.sy - ccy, lim = rc + pad; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }
            else if (m.sx < wx0 - pad || m.sx > wx1 + pad || m.sy < wy0 - pad || m.sy > wy1 + pad) continue;
            mk[n++] = m;
        }
        // THE ZOOM DECIDES (the user's rule, 2026-11-24) : at one zoom every entity of a kind has the SAME form -- dots
        // far, icons near -- whatever its neighbours. (A neighbour-based demotion made a camp a mix of arrows and dots,
        // unreadable : why is THIS one a dot ?) One exception : your party / trusts / pet at your feet stay DOTS, the
        // satellites around your arrow -- an icon there was buried under it.
        {
            const float ringR = TUNE("mm.me_size", 13.0f) * S * MSc * 0.5f + (TUNE("mm.sat_gap", 5.5f) + 7.0f) * S;
            for (int i = 0; i < n; ++i) {
                Mk& m = mk[i];
                const bool group = m.ring || (m.prio == 5 && m.fill != MK_MOB_FILL);
                if (!group || m.a <= 0.0f) continue;
                const float dx = m.sx - ccx, dy = m.sy - ccy, rr = ringR + m.sz * 0.5f;
                if (dx * dx + dy * dy < rr * rr) m.a = 0.0f;
            }
        }
        // LANDMARKS never hide (a Home Point next to a "???" stacked one on the other) : they push each other apart, just
        // enough to read side by side -- a few px, each stays near its spot
        for (int it = 0; it < 6; ++it) {
            bool moved = false;
            for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) {
                Mk& p = mk[i]; Mk& q = mk[j];
                if (p.prio != 9 || q.prio != 9) continue;
                float dx = q.sx - p.sx, dy = q.sy - p.sy; const float d = sqrtf(dx * dx + dy * dy), need = (p.sz + q.sz) * 0.45f;
                if (d >= need) continue;
                if (d < 0.01f) { dx = 1.0f; dy = 0.0f; } else { dx /= d; dy /= d; }
                const float push = (need - d) * 0.5f;
                p.sx -= dx * push; p.sy -= dy * push; q.sx += dx * push; q.sy += dy * push; moved = true;
            }
            if (!moved) break;
        }
        // EASE each entity from what it SHOWED last frame toward this frame's target : the morph is animated, never a snap
        for (int i = 0; i < n; ++i) {
            Mk& m = mk[i];
            if (!m.st || m.prio == 9) continue;
            m.a = m.prev + (m.a - m.prev) * ease;
            if (m.a < 0.004f) m.a = 0.0f; if (m.a > 0.996f) m.a = 1.0f;
            m.st->a = m.a;
        }
        // DOTS (least important first), faded out as their icon fades in
        color_state(dev);
        for (int pr = 1; pr <= 9; ++pr)
            for (int i = 0; i < n; ++i) {
                const Mk& m = mk[i];
                if (m.prio != pr || m.a >= 1.0f) continue;
                if (m.ring || (m.prio == 5 && m.fill != MK_MOB_FILL)) continue;   // party / alliance / trust / pet : drawn over your arrow, below
                const float da = m.a > 0.5f ? 0.0f : 1.0f - m.a * 2.0f;   // the dot leaves in the first half of the morph
                if (da <= 0.0f) continue;
                const u32 ka = (u32)(da * 255.0f + 0.5f);
                disc(dev, snap(m.sx), snap(m.sy), m.dr + 0.8f * S, (u32)(da * 153.0f) << 24);
                if (m.ring) disc(dev, snap(m.sx), snap(m.sy), m.dr + 0.6f * S, (ka << 24) | 0x00FFFFFFu);
                disc(dev, snap(m.sx), snap(m.sy), m.dr, (ka << 24) | (m.dotCol & 0x00FFFFFFu));
            }
        // the landmarks' halo (additive), under their icons
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        for (int i = 0; i < n; ++i) if (mk[i].prio == 9) disc_glow(dev, mk[i].sx, mk[i].sy, mk[i].sz * 0.25f, ((u32)TUNE("mm.halo_a", 40.0f) << 24) | 0x00FFF2C8u, mk[i].sz * TUNE("mm.halo_w", 0.32f));
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        // ICONS (least important first)
        tex_state(dev, mkAtlas_);
        for (int pr = 1; pr <= 9; ++pr)
            for (int i = 0; i < n; ++i) {
                const Mk& m = mk[i];
                if (m.prio != pr || m.a <= 0.0f) continue;
                // MORPH : the icon GROWS out of the dot (size from the dot's diameter to its own) while it fades in
                const float sz = (2.0f * m.dr) + (m.sz - 2.0f * m.dr) * m.a;
                const float ia = m.a < 0.5f ? m.a * 2.0f : 1.0f;     // opaque from half-way : no long ghost of two shapes
                const u32 ka = (u32)(ia * 255.0f + 0.5f);
                mk_cell(dev, m.frame >= 0 ? m.frame : m.fill, m.sx, m.sy + 1.0f, sz * 1.14f, m.ang, (u32)(ia * 140.0f) << 24);   // the dark under-shape
                mk_cell(dev, m.fill, m.sx, m.sy, sz, m.ang, (ka << 24) | (m.col & 0x00FFFFFFu));
                if (m.frame >= 0) mk_cell(dev, m.frame, m.sx, m.sy, sz, m.ang, (ka << 24) | (m.frameCol & 0x00FFFFFFu));
            }
        dSetTex(dev, 0, 0);
        s_groupN = 0;                                          // the group's dots, kept for the pass after your arrow
        for (int i = 0; i < n && s_groupN < 64; ++i) {
            const Mk& m = mk[i];
            if (m.a >= 1.0f || !(m.ring || (m.prio == 5 && m.fill != MK_MOB_FILL))) continue;
            GroupDot& d = s_group[s_groupN++];
            d.x = m.sx; d.y = m.sy; d.r = m.dr; d.a = 1.0f - m.a; d.col = m.dotCol;
        }
    }
    // ---- mob markers : the Arrow icon rotated by heading, tinted by claim colour ----
    if (!icons && g.mapEntN > 0 && mkMob_ && c.mmMob) {
        const float mkSz = clampf(2.4f + 1.0f * Z, 4.4f, 11.0f) * S * MSc;
        tex_state(dev, mkMob_);
        for (int e = 0; e < g.mapEntN; ++e) {
            const MapEntity& me = g.mapEnts[e];
            if (me.type != 3) continue;
            const float ex = (g.map.scale * me.x) / 5.0f - (float)g.map.offX;
            const float ey = (g.map.scale * -me.z) / 5.0f - (float)g.map.offY;
            const float sx = ccx + (ex - mapX) * pn, sy = ccy + (ey - mapY) * pn;
            const float mh = mkSz * 0.61f, pad = clipped ? mh : -mh;   // clipped: let it cross (the clip cuts the overhang) ; else keep the whole marker inside
            if (round) { const float dx = sx - ccx, dy = sy - ccy, lim = rc + pad; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }
            else if (sx < wx0 - pad || sx > wx1 + pad || sy < wy0 - pad || sy > wy1 + pad) continue;
            const float ang = eased_angle(me.id, me.heading + 1.5708f);
            tquad_rot(dev, snap(sx), snap(sy), mkSz * 1.22f, mkSz * 1.22f, ang, 0x99000000u);   // dark outline
            tquad_rot(dev, snap(sx), snap(sy), mkSz, mkSz, ang, mm_ent_color(me));              // colour fill
        }
        dSetTex(dev, 0, 0);
    }

    // ---- NPC / other-PC markers : a tinted dot, per-type visibility toggle ----
    if (!icons && g.mapEntN > 0 && (c.mmNPC || c.mmPC)) {
        color_state(dev);
        const float dF = clampf(0.9f + 0.42f * Z, 1.5f, 4.2f) * S * MSc, dO = dF + 0.5f * S;   // NPC / PC pastilles -- smaller
        for (int e = 0; e < g.mapEntN; ++e) {
            const MapEntity& me = g.mapEnts[e];
            if (me.type == 3) continue;
            if (me.type == 2 ? !c.mmNPC : !c.mmPC) continue;               // 2 = NPC, else PC
            const float ex = (g.map.scale * me.x) / 5.0f - (float)g.map.offX;
            const float ey = (g.map.scale * -me.z) / 5.0f - (float)g.map.offY;
            const float sx = ccx + (ex - mapX) * pn, sy = ccy + (ey - mapY) * pn;
            const float pad = clipped ? dO : -dO;   // clipped: let it cross (the clip cuts the overhang) ; else keep the whole dot inside
            if (round) { const float dx = sx - ccx, dy = sy - ccy, lim = rc + pad; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }
            else if (sx < wx0 - pad || sx > wx1 + pad || sy < wy0 - pad || sy > wy1 + pad) continue;
            disc(dev, snap(sx), snap(sy), dO, 0x88000000u);
            disc(dev, snap(sx), snap(sy), dF, mm_ent_color(me));
        }
    }
    if (vpSet) dSetViewport(dev, vpSave);   // restore the full viewport

    // ---- player marker : the Location pin at the centre, rotated to face the heading, navy + halo ----
    if (icons) {                                                  // YOU : the navigation arrow, white, gold frame
        const float pSz = snap(TUNE("mm.me_size", 13.0f) * S * MSc), pang = g.meHeading + 1.5708f;   // 17 covered the trusts at your feet
        tex_state(dev, mkAtlas_);
        mk_cell(dev, MK_ME_FRAME, ccx, ccy + 1.0f, pSz * 1.14f, pang, 0x8C000000u);
        mk_cell(dev, MK_ME_FILL, ccx, ccy, pSz, pang, 0xFFFFFFFFu);
        mk_cell(dev, MK_ME_FRAME, ccx, ccy, pSz, pang, 0xFFFFFFFFu);
        dSetTex(dev, 0, 0);
        // your party / alliance / trusts / pet as DOTS, ON TOP of your arrow : they walk at your feet (1-2 yalms = a
        // few px), under the arrow they vanished. A white rim keeps each one readable against the arrow.
        // SATELLITES : a member closer than the arrow's own radius is pushed out to a small ring around it, in ITS
        // direction from you, then spread so no two dots overlap -- the arrow stays visible, every trust is counted.
        {
            const float R = pSz * 0.5f + TUNE("mm.sat_gap", 5.5f) * S;
            int idx[64]; float ang[64]; int nn = 0;
            for (int i = 0; i < s_groupN; ++i) {
                const float dx = s_group[i].x - ccx, dy = s_group[i].y - ccy;
                if (dx * dx + dy * dy >= R * R) continue;
                idx[nn] = i; ang[nn] = (dx * dx + dy * dy > 0.25f) ? atan2f(dy, dx) : (-1.5708f + 0.9f * nn); ++nn;
            }
            for (int a = 1; a < nn; ++a) for (int b = a; b > 0 && ang[b] < ang[b - 1]; --b) {   // by angle
                float t = ang[b]; ang[b] = ang[b - 1]; ang[b - 1] = t; int u = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = u; }
            if (nn > 1) {
                const float need = (2.0f * s_group[idx[0]].r + 2.6f * S) / R;          // the arc two dots need side by side
                for (int pass = 0; pass < 6; ++pass)
                    for (int k = 0; k < nn; ++k) {
                        const int k2 = (k + 1) % nn;
                        float gap = ang[k2] - ang[k]; if (k2 == 0) gap += 6.2832f;
                        if (gap < need) { const float push = (need - gap) * 0.5f; ang[k] -= push; ang[k2] += push; }
                    }
            }
            for (int k = 0; k < nn; ++k) { GroupDot& d = s_group[idx[k]]; d.x = ccx + cosf(ang[k]) * R; d.y = ccy + sinf(ang[k]) * R; }
            // then ALL the group's dots push each other apart (a few px at most) until none overlaps, and none
            // re-enters your arrow : members just past the ring were still stacked on each other
            for (int it = 0; it < 8; ++it) {
                bool moved = false;
                for (int i = 0; i < s_groupN; ++i) for (int j = i + 1; j < s_groupN; ++j) {
                    GroupDot& p = s_group[i]; GroupDot& q = s_group[j];
                    float dx = q.x - p.x, dy = q.y - p.y; const float d = sqrtf(dx * dx + dy * dy), need = p.r + q.r + 1.6f * S;
                    if (d >= need) continue;
                    if (d < 0.01f) { dx = 1.0f; dy = 0.0f; } else { dx /= d; dy /= d; }
                    const float push = (need - d) * 0.5f;
                    p.x -= dx * push; p.y -= dy * push; q.x += dx * push; q.y += dy * push; moved = true;
                }
                for (int i = 0; i < s_groupN; ++i) {                // and out of the arrow
                    GroupDot& p = s_group[i]; const float dx = p.x - ccx, dy = p.y - ccy, d = sqrtf(dx * dx + dy * dy);
                    if (d < R && d > 0.01f) { p.x = ccx + dx / d * R; p.y = ccy + dy / d * R; }
                }
                if (!moved) break;
            }
        }
        color_state(dev);
        for (int i = 0; i < s_groupN; ++i) {
            const GroupDot& d = s_group[i];
            const u32 ka = (u32)(d.a * 255.0f + 0.5f);
            disc(dev, snap(d.x), snap(d.y), d.r + 1.1f * S, (u32)(d.a * 170.0f) << 24);
            disc(dev, snap(d.x), snap(d.y), d.r + 0.5f * S, (ka << 24) | 0x00FFFFFFu);
            disc(dev, snap(d.x), snap(d.y), d.r, (ka << 24) | (d.col & 0x00FFFFFFu));
        }
    } else if (mkPlayer_) {
        const float pSz = clampf(3.2f + 1.3f * Z, 8.0f, 15.0f) * S * MSc;
        const float pang = g.meHeading + 1.5708f;
        tex_state(dev, mkPlayer_);
        tquad_rot(dev, snap(ccx), snap(ccy), pSz * 1.2f, pSz * 1.2f, pang, 0xF0FFFFFFu);   // white halo behind
        tquad_rot(dev, snap(ccx), snap(ccy), pSz,        pSz,        pang, 0xFF12224Au);    // navy pin
        dSetTex(dev, 0, 0);
    }

    PERF_END("mm.markers");
    // ---- the map grid square, "(F-10)" -- the native compass's line, the client's own formula (FUN_05538C10) :
    //      column 'A' + (px - 16) / 32, row 1 + (py - 16) / 32 on the 512 px native map, C division (toward zero) ----
    PERF_BEGIN("mm.ring");
    if (astral) {                                              // the ring, its graduations, the glint, the cardinal medallions
        const bool grey = c.mmFrame != 0 && astG_[AS_BASE] && astG_[AS_TICKS] && astG_[AS_GLINT];   // the theme's metal : the grey kit, tinted
        const u32 tint = grey ? dm_toward(g_dm.body, 0xFFFFFFFFu, 0.35f) : 0xFFFFFFFFu;
        // (no horns : the modules are mounted BEHIND the compass now, nothing joins them on its face)
        draw_astral_ring(dev, grey ? astG_[AS_BASE] : ast_[AS_BASE],  ccx, ccy, rc, r, 416.0f, 504.0f, 0.0f, tint);
        draw_astral_ring(dev, grey ? astG_[AS_TICKS] : ast_[AS_TICKS], ccx, ccy, rc, r, 424.0f, 474.0f, 0.0f, tint);      // north-up : the camera rotation will turn this one
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        draw_astral_ring(dev, grey ? astG_[AS_GLINT] : ast_[AS_GLINT], ccx, ccy, rc, r, 418.0f, 488.0f, 0.0f, tint);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        // the medallions : on the kit's cardinal circle (470, mapped like the ring), sized by the outer radius x the
        // Cardinal-size slider
        const float rr = rc + (470.0f - 420.0f) * (r - rc) / 80.0f, ps = 128.0f * (2.0f * r / 1000.0f) * clampf(c.mmCardSz, 0.5f, 2.0f);
        static const float DX[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, DY[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
        for (int i = 0; i < 4 && !dial; ++i) {                          // the dial engraves its letters instead
            const float px0 = snap(ccx + DX[i] * rr - ps * 0.5f), py0 = snap(ccy + DY[i] * rr - ps * 0.5f);
            tex_state(dev, ast_[AS_PAST]);   tquad(dev, px0, py0, ps, ps, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
            tex_state(dev, ast_[AS_LN + i]); tquad(dev, px0, py0, ps, ps, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
        }
        dSetTex(dev, 0, 0);
    }
    PERF_END("mm.ring");
    if (dial) {
        static float s_lastZoom = 0.0f, s_zoomT = -10.0f;                  // the zoom readout lives 1.6 s after a change
        if (c.mmZoom != s_lastZoom) { if (s_lastZoom != 0.0f) s_zoomT = f.t; s_lastZoom = c.mmZoom; }
        float za = 1.0f - (f.t - s_zoomT - 1.0f) / 0.6f; za = clampf(za, 0.0f, 1.0f);
        if (f.t < s_zoomT) za = 0.0f;                                       // f.t wrapped
        const VanaClock dv = dial_vana(g.vana);
        if (dialSq) { color_state(dev); rrect_stroke(dev, wx0 - 0.5f, wy0 - 0.5f, wx1 - wx0 + 1.0f, wy1 - wy0 + 1.0f, snap(2.0f * S), 0xC0140F08u, snap(1.4f * S)); }   // the inner edge's shadow on the map
        ensure_raw_tex_mip(dev, wxTex_, wxR_, plugin_path_r("assets\\weather_icons.raw"), 1024, 128);   // the weather orbs' HD icons
        { PERF_SCOPE("mm.dial"); draw_dial_instrument(dev, f, c, g, dv, elemTex_, wxTex_, mapX, mapY, ccx, ccy, dialSq ? r - fi : rc, r, S, za, dialSq, 1); }
    }
    else if (compact && ibH > 0.0f) draw_info_band(dev, f, c, g, mapX, mapY, ix, ibY, Wbox, S);   // the square lives in the band then
    else if (c.mmGrid && g.map.scale != 0 && f.font) {
        const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
        if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) {
            // the SAME face as the info band's square (serif), in a small dark pill -- not a heavy outlined "(F-10)"
            char gb[12]; _snprintf(gb, sizeof(gb), "%c-%d", 'A' + gc, gr + 1); gb[sizeof(gb) - 1] = 0;
            Font* sf = band_font(f, MM_TIME);
            const float gsz = snap(13.0f * S), tw = sf->measure(gb, gsz), ph = snap(17.0f * S), pw = tw + snap(14.0f * S);
            const float gy = round ? (ccy + rc - snap(14.0f * S)) : (wy1 - snap(13.0f * S));
            color_state(dev);
            rrect(dev, snap(ccx - pw * 0.5f), snap(gy - ph * 0.5f), snap(pw), ph, ph * 0.5f, 0xB0141820u, 0xC00C0F14u);
            rrect_stroke(dev, snap(ccx - pw * 0.5f), snap(gy - ph * 0.5f), snap(pw), ph, ph * 0.5f, 0x40FFFFFFu, 1.0f);
            sf->begin(dev);
            sf->draw_c(dev, snap(ccx), snap(gy), gb, gsz, 0xFFF4EEDFu, 0xFF000000u, 1.0f * S);
        }
    }

    // ---- SQUARE frame stroke on top : custom colour, or the fallback when an FFXI skin wasn't ready yet ----
    // (round is drawn as a solid ring BEFORE the map above ; a real themed square chrome sets themedOk.)
    if (!themedOk && !round && c.mmFrame != 0 && !dialSq) {
        const u32 fc = mm_frame_color(c, f);
        color_state(dev);
        const float sqbw = snap(1.6f * S * clampf(c.mmSqBorder, 0.5f, 2.0f));   // Square border-width slider
        rrect_stroke(dev, mapLeft - sqbw, mapTop - sqbw, mapD + 2.0f * sqbw, mapD + 2.0f * sqbw, snap(3.0f * S) + sqbw, fc, sqbw);   // grow the border OUTWARD from the map edge (was drawn inward -> it ate into the map)
    }
}

// ================= Help live samples : draw the REAL minimap elements (SAME colours / marker draw as the
// widget) for the Help tab (config_page.cpp). Everything below reuses the widget's marker/moon/day renderers
// so the sample and the live minimap can never drift. No game memory is touched -- the data is synthetic.

// Caller caches the handles AND the retry state, and forgets/re-arms both on device-lost. Safe to call every
// frame : each handle self-gates on its own bounded, time-spaced budget (ui/tex_retry.h). The `if (!tex)` guard
// alone was NOT enough -- the three call sites wrapped this in a one-shot `if (!mmTried_)` latch, so a single
// miss (device not ready on the first Help draw after a zone-in, or an .raw briefly locked by the updater) left
// the marker/atlas samples blank for the whole session. Same shape as target_help_textures.
void minimap_help_textures(u32 dev, u32& mkPlayer, u32& mkMob, u32& elemTex,
                           TexRetry& rPlayer, TexRetry& rMob, TexRetry& rElem) {
    ensure_raw_tex_mip(dev, mkPlayer, rPlayer, MK_PLAYER_PATH(),  64,  64);
    ensure_raw_tex_mip(dev, mkMob,    rMob,    MK_MOB_PATH(),     64,  64);
    ensure_raw_tex_mip(dev, elemTex,  rElem,   ELEM_ATLAS_PATH(), 256, 32);
}

// a little round minimap : the themed frame ring + a dark lens (with a faint terrain feel) + the player pin at
// the centre + a cyan PC, a green NPC (dots) and a gold + a red mob (arrows) orbiting via `t`. Reads like a
// tiny real minimap. Frame colour follows the party/alliance box theme (mm_frame_color), like the widget.
void minimap_help_disc(u32 dev, const Frame& f, Font* fo, u32 mkPlayer, u32 mkMob, u32 mapTex, float cx, float cy, float r, float t) {
    (void)fo; (void)t;
    // Faithful sample : mirror the LIVE widget's SHAPE (round / square), FRAME (brass bezel / themed ring / custom
    // colour ring / themed square chrome) and zoom, so the Help minimap matches the one you actually run.
    // `mapTex` = the Help's own copy of the current-zone map (loaded from the ROM DAT by config_page.cpp).
    const UiConfig& c = ui_config();
    const GameState* g = f.game;
    const bool  round = (c.mmShape == 1);
    const float Ss = r / 110.0f;                                       // scale the frame like the live lens (in-game r ~= 110*S)
    const float mapD = 2.0f * r, mapLeft = cx - r, mapTop = cy - r, mapPx = 512.0f;
    const float Z  = clampf(c.mmZoom, 1.0f, 24.0f);                   // follow the user's configured zoom
    const float pn = (mapD / mapPx) * Z;                             // screen px per native map px (identical to the widget)
    color_state(dev);

    // ---- frame : config-aware, exactly like Minimap::draw (bezel when round+clock, themed/custom ring for round,
    //      themed window chrome for square) ----
    bool themedOk = false; float fi = 0.0f, rc = r;
    bool astral = round && c.mmClock && c.mmBezel;                     // the Astral kit, borrowed from the live widget
    for (int i = 0; i < AS_N && astral; ++i) if (!s_shAst[i]) astral = false;
    if (astral) rc = r - r * (80.0f / 500.0f) * clampf(c.mmBezelW, 0.5f, 2.0f);
    else if (round) {
        if (c.mmClock && c.mmBezel) { const float bw = snap(22.0f * Ss * clampf(c.mmBezelW, 0.5f, 2.0f)); draw_brass_bezel(dev, f, cx, cy, r, bw, Ss); rc = r - bw; }
        else if (c.mmClock && !c.mmBezel) { /* no circle at all : lens fills the full radius (rc stays r) */ }
        else if (c.mmFrame != 0) {
            const bool procThemed = (c.mmFrame == 1 && window_theme_is_proc(c.skinTheme));
            const float ringT = snap((procThemed ? 7.0f : 5.0f) * Ss); rc = r - ringT;
            if (procThemed) draw_box_border_ring(dev, c.skinTheme, cx, cy, r, rc, c.skinLum, c.skinHue);
            else { color_state(dev); const u32 fc = mm_frame_color(c, f), rim = mm_darken(fc, 0.55f);
                   rrect(dev, cx - r, cy - r, 2.0f * r, 2.0f * r, r, rim, rim, snap(1.2f * Ss));
                   const float ri = r - snap(1.0f * Ss); rrect(dev, cx - ri, cy - ri, 2.0f * ri, 2.0f * ri, ri, fc, fc, snap(1.2f * Ss)); }
        }
    } else if (c.mmFrame == 1) {                                       // square + themed window chrome
        if (window_theme_is_proc(c.skinTheme)) { draw_proc_window(dev, c.skinTheme, mapLeft, mapTop, mapD, mapD, 0xFFFFFFFFu, false, true, c.skinLum, c.skinHue); fi = snap(6.0f * Ss); themedOk = true; }
        else if (f.skin && f.skin->ready()) { draw_window(dev, *f.skin, mapLeft, mapTop, mapD, mapD, 0xFFFFFFFFu, Ss, false, true); fi = snap(15.0f * Ss); themedOk = true; }
        if (themedOk) fi += frame_inset();                    // a frame theme's thicker border : the map clears it too (0 = normal frames)
    }
    const float wx0 = mapLeft + fi, wy0 = mapTop + fi, wx1 = mapLeft + mapD - fi, wy1 = mapTop + mapD - fi;

    // ---- dark lens backdrop (kept solid so the map reads cleanly inside the config panel) ----
    if (!themedOk) { color_state(dev); if (round) disc(dev, cx, cy, rc, 0xFF0A0E22u); else grad_quad(dev, wx0, wy0, wx1 - wx0, wy1 - wy0, 0xFF0A0E22u, 0xFF0A0E22u, 0xFF0A0E22u, 0xFF0A0E22u); }

    // ---- the current-zone map, player-centred, north-up, V-flipped : round FAN or square clamped quad ----
    const float mapX = (g && g->map.valid) ? (g->map.scale * g->meX) / 5.0f - (float)g->map.offX : 0.0f;
    const float mapY = (g && g->map.valid) ? (g->map.scale * -g->meZ) / 5.0f - (float)g->map.offY : 0.0f;
    if (mapTex && g && g->map.valid) {
        const float qs = mapPx * pn, qx = cx - mapX * pn, qy = cy - mapY * pn;
        tex_state(dev, mapTex);
        if (round) { const float u0c = mapX / mapPx, v0c = 1.0f - mapY / mapPx; tdisc(dev, cx, cy, rc, u0c, v0c, 1.0f / qs, -1.0f / qs, 0xFFFFFFFFu); }
        else {
            const float bx0 = qx > wx0 ? qx : wx0, by0 = qy > wy0 ? qy : wy0;
            const float bx1 = (qx + qs) < wx1 ? (qx + qs) : wx1, by1 = (qy + qs) < wy1 ? (qy + qs) : wy1;
            if (bx1 > bx0 && by1 > by0) {
                const float u0 = (bx0 - qx) / qs, u1 = (bx1 - qx) / qs, v0 = 1.0f - (by0 - qy) / qs, v1 = 1.0f - (by1 - qy) / qs;
                tquad(dev, snap(bx0), snap(by0), snap(bx1) - snap(bx0), snap(by1) - snap(by0), u0, u1, v0, v1, 0xFFFFFFFFu, 0xFFFFFFFFu);
            }
        }
        if (astral) draw_astral_shadow(dev, s_shAst[AS_SHADOW], cx, cy, rc);
        dSetTex(dev, 0, 0); color_state(dev);
    } else if (round) {                                               // no map -> a plain lens hint
        disc_glow(dev, cx, cy, rc * 0.62f, 0x304A569Cu, rc * 0.5f);
    }

    // ---- ICON markers : the widget's own classification (mk_classify) and zoom rule, without the animation ----
    const bool icons = (c.mmMarkStyle == 1) && s_shAtlas;
    if (icons && g && g->map.valid) {
        const float MSc = clampf(c.mmMarkerScale, 0.5f, 2.0f);
        const float ppy = ((float)g->map.scale / 5.0f) * pn, T0 = (TUNE("mm.lod_p1", 0.5f) + TUNE("mm.lod_p2", 1.1f)) * 0.5f;
        for (int pass = 0; pass < 2; ++pass) {                       // dots, then icons
            if (pass == 0) color_state(dev); else tex_state(dev, s_shAtlas);
            for (int e = 0; e < g->mapEntN; ++e) {
                const MapEntity& me = g->mapEnts[e];
                MkClass k; if (!mk_classify(me, c, k)) continue;
                const bool icon = k.cf == 0.0f || (k.cf > 0.0f && ppy >= T0 * k.cf);
                if (icon != (pass == 1)) continue;
                const float ex = (g->map.scale * me.x) / 5.0f - (float)g->map.offX, ey = (g->map.scale * -me.z) / 5.0f - (float)g->map.offY;
                const float sx = cx + (ex - mapX) * pn, sy = cy + (ey - mapY) * pn;
                const float sz = k.isz * Ss * MSc * (k.prio == 9 ? clampf(0.62f + 0.4f * ppy, 0.62f, 1.0f) : 1.0f), dr = k.dr * Ss * MSc, half = icon ? sz * 0.5f : dr + 1.0f;
                if (round) { const float dx = sx - cx, dy = sy - cy, lim = rc - half; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }
                else if (sx < wx0 + half || sx > wx1 - half || sy < wy0 + half || sy > wy1 - half) continue;
                if (!icon) {
                    disc(dev, snap(sx), snap(sy), dr + 0.8f * Ss, 0x99000000u);
                    if (k.ring) disc(dev, snap(sx), snap(sy), dr + 0.6f * Ss, 0xFFFFFFFFu);
                    disc(dev, snap(sx), snap(sy), dr, k.col | 0xFF000000u);
                } else {
                    const float ang = k.dir ? me.heading + 1.5708f : 0.0f;
                    mk_cell(dev, k.frame >= 0 ? k.frame : k.fill, sx, sy + 1.0f, sz * 1.14f, ang, 0x8C000000u);
                    mk_cell(dev, k.fill, sx, sy, sz, ang, k.col);
                    if (k.frame >= 0) mk_cell(dev, k.frame, sx, sy, sz, ang, k.frameCol);
                }
            }
        }
        dSetTex(dev, 0, 0);
    }
    // ---- CLASSIC markers + player pin : same transform / colours as the widget ; round -> circle clip, square -> rect clip ----
    if (!icons && g && g->map.valid) {
        const float MSc = clampf(c.mmMarkerScale, 0.5f, 2.0f);
        if (mkMob && c.mmMob) {                                       // mobs = the Arrow icon, rotated by heading, tinted by claim
            const float mkSz = clampf(2.4f + 1.0f * Z, 4.4f, 11.0f) * Ss * MSc;
            tex_state(dev, mkMob);
            for (int e = 0; e < g->mapEntN; ++e) {
                const MapEntity& me = g->mapEnts[e]; if (me.type != 3) continue;
                const float ex = (g->map.scale * me.x) / 5.0f - (float)g->map.offX, ey = (g->map.scale * -me.z) / 5.0f - (float)g->map.offY;
                const float sx = cx + (ex - mapX) * pn, sy = cy + (ey - mapY) * pn, m = mkSz * 0.61f;
                if (round) { const float dx = sx - cx, dy = sy - cy, lim = rc - m; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }
                else if (sx < wx0 + m || sx > wx1 - m || sy < wy0 + m || sy > wy1 - m) continue;
                const float ang = me.heading + 1.5708f;
                tquad_rot(dev, snap(sx), snap(sy), mkSz * 1.22f, mkSz * 1.22f, ang, 0x99000000u);
                tquad_rot(dev, snap(sx), snap(sy), mkSz, mkSz, ang, mm_ent_color(me));
            }
            dSetTex(dev, 0, 0);
        }
        color_state(dev);
        const float dF = clampf(0.9f + 0.42f * Z, 1.5f, 4.2f) * Ss * MSc, dO = dF + 0.5f * Ss;   // NPC / PC pastilles
        for (int e = 0; e < g->mapEntN; ++e) {
            const MapEntity& me = g->mapEnts[e]; if (me.type == 3) continue;
            if (me.type == 2 ? !c.mmNPC : !c.mmPC) continue;
            const float ex = (g->map.scale * me.x) / 5.0f - (float)g->map.offX, ey = (g->map.scale * -me.z) / 5.0f - (float)g->map.offY;
            const float sx = cx + (ex - mapX) * pn, sy = cy + (ey - mapY) * pn;
            if (round) { const float dx = sx - cx, dy = sy - cy, lim = rc - dO; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }
            else if (sx < wx0 + dO || sx > wx1 - dO || sy < wy0 + dO || sy > wy1 - dO) continue;
            disc(dev, snap(sx), snap(sy), dO, 0x88000000u);
            disc(dev, snap(sx), snap(sy), dF, mm_ent_color(me));
        }
    }
    if (icons) {                                                                   // YOU : the navigation arrow (icons)
        const float pSz = snap(TUNE("mm.me_size", 13.0f) * Ss * clampf(c.mmMarkerScale, 0.5f, 2.0f)), pang = (g ? g->meHeading : 0.0f) + 1.5708f;
        tex_state(dev, s_shAtlas);
        mk_cell(dev, MK_ME_FRAME, cx, cy + 1.0f, pSz * 1.14f, pang, 0x8C000000u);
        mk_cell(dev, MK_ME_FILL, cx, cy, pSz, pang, 0xFFFFFFFFu);
        mk_cell(dev, MK_ME_FRAME, cx, cy, pSz, pang, 0xFFFFFFFFu);
        dSetTex(dev, 0, 0);
    } else if (mkPlayer) {                                                         // player Location pin at the centre (navy + halo)
        tex_state(dev, mkPlayer);
        const float pSz = snap(14.0f), pang = (g ? g->meHeading : 0.0f) + 1.5708f;
        tquad_rot(dev, snap(cx), snap(cy), pSz * 1.2f, pSz * 1.2f, pang, 0xF0FFFFFFu);
        tquad_rot(dev, snap(cx), snap(cy), pSz, pSz, pang, 0xFF12224Au);
        dSetTex(dev, 0, 0);
    }
    if (astral) {                                                                  // the Astral ring over the lens, like the widget
        draw_astral_ring(dev, s_shAst[AS_BASE],  cx, cy, rc, r, 416.0f, 504.0f, 0.0f);
        draw_astral_ring(dev, s_shAst[AS_TICKS], cx, cy, rc, r, 424.0f, 474.0f, 0.0f);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        draw_astral_ring(dev, s_shAst[AS_GLINT], cx, cy, rc, r, 418.0f, 488.0f, 0.0f);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        const float rr = rc + (470.0f - 420.0f) * (r - rc) / 80.0f, ps = 128.0f * (2.0f * r / 1000.0f) * clampf(c.mmCardSz, 0.5f, 2.0f);
        static const float DX[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, DY[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
        for (int i = 0; i < 4; ++i) {
            const float px0 = snap(cx + DX[i] * rr - ps * 0.5f), py0 = snap(cy + DY[i] * rr - ps * 0.5f);
            tex_state(dev, s_shAst[AS_PAST]);   tquad(dev, px0, py0, ps, ps, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
            tex_state(dev, s_shAst[AS_LN + i]); tquad(dev, px0, py0, ps, ps, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
        }
        dSetTex(dev, 0, 0);
    }
    color_state(dev);
}

// the marker legend : one row each = the real marker (dot / mob Arrow / player pin, in its model colour) + its
// meaning. Clips per row against [top,bot] (like the leader-dots sample) so a long list scrolls cleanly.
float minimap_help_legend(u32 dev, Font* fo, u32 mkPlayer, u32 mkMob, float x, float y, float rowH, float top, float bot, int lang) {
    if (ui_config().mmMarkStyle == 1 && s_shAtlas) {                  // ICONS : the legend of what the map really draws
        // GROUPED CARDS over the full width : People / Monsters / Places, each a titled column of rows with a 34 px icon,
        // its name and one line of what it means. The column count follows the width (3, 2 or 1).
        struct IRow { int fill, frame; u32 col, frameCol; bool dot; const char* en; const char* fr; const char* den; const char* dfr; };
        static const IRow PEOPLE[] = {
            { MK_ME_FILL,    MK_ME_FRAME,    0xFFFFFFFFu, 0xFFFFFFFFu, false, "You", "Toi", "points where you face", "pointe l\xC3\xA0 o\xC3\xB9 tu regardes" },
            { MK_PARTY_FILL, MK_PARTY_FRAME, 0xFF5AB4FFu, 0xFFFFFFFFu, true,  "Party member", "Membre de ta party", "blue dot, white rim", "point bleu, liser\xC3\xA9 blanc" },
            { MK_ALLY_FILL,  MK_ALLY_FRAME,  0xFFB48CFFu, 0xFFFFFFFFu, true,  "Alliance member", "Membre de l'alliance", "lavender dot, white rim", "point lavande, liser\xC3\xA9 blanc" },
            { MK_TRUST_FILL, MK_TRUST_FRAME, 0xFF9CCBFFu, 0xFFFFFFFFu, true,  "Trust", "Trust", "pale blue dot, white rim", "point bleu p\xC3\xA2le, liser\xC3\xA9 blanc" },
            { MK_PET_FILL,   MK_PET_FRAME,   0xFF4FD1C5u, 0xFFFFFFFFu, true,  "Your pet", "Ton familier", "teal dot, white rim", "point turquoise, liser\xC3\xA9 blanc" },
            { MK_PC_OTHER,   -1,             0xFFD8DEE6u, 0,           true,  "Other player", "Autre joueur", "a small grey dot", "un petit point gris" },
            { -1,            -1,             0xFF5ED37Cu, 0,           true,  "NPC", "PNJ", "a green dot, always", "un point vert, toujours" },
        };
        static const IRow MOBS[] = {
            { MK_MOB_FILL, MK_MOB_FRAME, 0xFFFFE600u, 0xFF4A3A22u, false, "Unclaimed", "Libre", "free to pull", "libre de pull" },
            { MK_MOB_FILL, MK_MOB_FRAME, 0xFFFF5A4Eu, 0xFF4A3A22u, false, "Yours / your party's", "\xC3\x80 toi / ta party", "claimed by you, or fighting you", "r\xC3\xA9""clam\xC3\xA9 par toi, ou te combat" },
            { MK_MOB_FILL, MK_MOB_FRAME, 0xFFE86BE0u, 0xFF4A3A22u, false, "Another player's", "Pris par un autre", "hands off", "ne pas toucher" },
            { MK_MOB_FILL, MK_MOB_FRAME, 0xFFFFE600u, 0xFFFFD34Au, false, "Notorious monster", "Monstre notable (NM)", "bigger, a bright gold frame", "plus grand, liser\xC3\xA9 or vif" },
        };
        static const IRow PLACES[] = {
            { MK_HOMEPOINT, -1, 0xFFFFFFFFu, 0, false, "Home Point", "Home Point", "warp and return point", "point de retour et de t\xC3\xA9l\xC3\xA9port" },
            { MK_WAYPOINT,  -1, 0xFFFFFFFFu, 0, false, "Waypoint", "Waypoint", "Adoulin travel", "voyage d'Adoulin" },
            { MK_GUIDE,     -1, 0xFFFFFFFFu, 0, false, "Survival Guide", "Survival Guide", "fast travel", "voyage rapide" },
            { MK_MOOGLE,    -1, 0xFFFFFFFFu, 0, false, "Moogle", "Moogle", "your Mog House services", "les services de ta Mog House" },
            { MK_QUEST,     -1, 0xFFFFFFFFu, 0, false, "???", "???", "something to examine", "quelque chose \xC3\xA0 examiner" },
        };
        struct Grp { const char* en; const char* fr; const IRow* r; int n; };
        const Grp G[3] = { { "PEOPLE", "PERSONNES", PEOPLE, 7 }, { "MONSTERS", "MONSTRES", MOBS, 4 }, { "PLACES", "LIEUX", PLACES, 5 } };
        const float W = rowH > 100.0f ? rowH : 0.0f;                  // (rowH carries the available WIDTH when > 100 : see config_page.cpp)
        const float avail = W > 0.0f ? W : snap(900.0f);
        const int cols = avail >= snap(840.0f) ? 3 : (avail >= snap(560.0f) ? 2 : 1);
        const float gap = snap(18.0f), cw = snap((avail - gap * (float)(cols - 1)) / (float)cols);
        const float rh = snap(46.0f), hh = snap(30.0f), isz = snap(34.0f);
        float colY[3] = { y, y, y };
        for (int gi = 0; gi < 3; ++gi) {
            int c = 0; for (int k = 1; k < cols; ++k) if (colY[k] < colY[c]) c = k;     // the shortest column takes the next group
            const float cx0 = x + (cw + gap) * (float)c;
            float yy = colY[c];
            const float cardH = hh + rh * (float)G[gi].n + snap(8.0f);
            if (yy + cardH >= top && yy <= bot) {
                color_state(dev);
                rrect(dev, cx0, yy, cw, cardH, snap(8.0f), 0x14FFFFFFu, 0x08FFFFFFu);
                rrect_stroke(dev, cx0, yy, cw, cardH, snap(8.0f), 0x1EFFFFFFu, 1.0f);
                if (fo && yy + hh >= top && yy <= bot) { fo->begin(dev); fo->draw_lc(dev, snap(cx0 + 14.0f), snap(yy + hh * 0.55f), lang == 1 ? G[gi].fr : G[gi].en, snap(12.0f), 0xFFC9A55Au, 0xFF000000u, 0.6f); }
            }
            yy += hh;
            for (int i = 0; i < G[gi].n; ++i) {
                const IRow& R = G[gi].r[i];
                if (yy >= top && yy + rh <= bot) {
                    const float mcx = cx0 + snap(14.0f) + isz * 0.5f, mcy = yy + rh * 0.5f;
                    if (R.dot) {
                        const bool rim = R.col != 0xFF5ED37Cu && R.col != 0xFFD8DEE6u;   // your own people wear the white rim, like on the map
                        color_state(dev); disc(dev, snap(mcx), snap(mcy), snap(8.0f), 0x99000000u);
                        if (rim) disc(dev, snap(mcx), snap(mcy), snap(7.0f), 0xFFFFFFFFu);
                        disc(dev, snap(mcx), snap(mcy), snap(5.8f), R.col);
                    }
                    else {
                        tex_state(dev, s_shAtlas);
                        mk_cell(dev, R.frame >= 0 ? R.frame : R.fill, mcx, mcy + 1.5f, isz * 1.12f, 0.0f, 0x8C000000u);
                        mk_cell(dev, R.fill, mcx, mcy, isz, 0.0f, R.col);
                        if (R.frame >= 0) mk_cell(dev, R.frame, mcx, mcy, isz, 0.0f, R.frameCol);
                        dSetTex(dev, 0, 0);
                    }
                    if (fo) {
                        fo->begin(dev);
                        const float tx = cx0 + snap(14.0f) + isz + snap(14.0f);
                        fo->draw_lc(dev, snap(tx), snap(mcy - 8.0f), lang == 1 ? R.fr : R.en, snap(15.0f), 0xFFF0F3F7u, 0xFF000000u, 1.0f);
                        fo->draw_lc(dev, snap(tx), snap(mcy + 10.0f), lang == 1 ? R.dfr : R.den, snap(12.0f), 0xFF8E9AAAu, 0xFF000000u, 0.6f);
                    }
                }
                yy += rh;
            }
            colY[c] = yy + snap(8.0f) + gap;
        }
        float yMax = y; for (int k = 0; k < cols; ++k) if (colY[k] > yMax) yMax = colY[k];
        color_state(dev);
        return yMax - gap;
    }
    struct Row { int kind; u32 col; const char* en; const char* fr; };            // kind : 0 dot, 1 mob arrow, 2 player pin
    static const Row rows[8] = {
        { 2, 0xFF12224Au, "Your position",                        "Ta position" },
        { 0, 0xFFB6EEF0u, "Party member",                         "Membre de ta party" },
        { 0, 0xFF7AB7F1u, "Another party (alliance)",             "Autre party (alliance)" },
        { 0, 0xFFFFFFFFu, "Solo player",                          "Joueur solo" },
        { 0, 0xFFBCF4BCu, "NPC or object",                        "PNJ ou objet" },
        { 1, 0xFFF2F2B7u, "Monster, unclaimed",                   "Monstre, non r\xC3\xA9""clam\xC3\xA9" },
        { 1, 0xFFF0787Au, "Monster you or your party claim",      "Monstre r\xC3\xA9""clam\xC3\xA9 par toi ou ta party" },
        { 1, 0xFFF67AF5u, "Monster another player claims",        "Monstre r\xC3\xA9""clam\xC3\xA9 par un autre joueur" },
    };
    for (int i = 0; i < 8; ++i) {
        if (y >= top && y + rowH <= bot) {
            const float mcx = x + snap(11.0f), mcy = y + rowH * 0.5f;
            if (rows[i].kind == 0) {                                               // NPC / PC = a tinted dot
                color_state(dev);
                disc(dev, snap(mcx), snap(mcy), snap(5.0f), 0x88000000u);
                disc(dev, snap(mcx), snap(mcy), snap(3.6f), rows[i].col);
            } else if (rows[i].kind == 1 && mkMob) {                              // mob = the Arrow icon, tinted
                tex_state(dev, mkMob);
                tquad_rot(dev, snap(mcx), snap(mcy), snap(15.0f), snap(15.0f), 0.0f, 0x99000000u);
                tquad_rot(dev, snap(mcx), snap(mcy), snap(13.0f), snap(13.0f), 0.0f, rows[i].col);
                dSetTex(dev, 0, 0);
            } else if (rows[i].kind == 2 && mkPlayer) {                           // you = the Location pin
                tex_state(dev, mkPlayer);
                tquad_rot(dev, snap(mcx), snap(mcy), snap(16.0f), snap(16.0f), 0.0f, 0xF0FFFFFFu);
                tquad_rot(dev, snap(mcx), snap(mcy), snap(13.0f), snap(13.0f), 0.0f, rows[i].col);
                dSetTex(dev, 0, 0);
            }
            if (fo) { fo->begin(dev); fo->draw_lc(dev, snap(x + snap(30.0f)), snap(mcy), (lang == 1) ? rows[i].fr : rows[i].en, snap(14.0f), 0xFFE7ECF0u, 0xFF000000u, 1.0f); }
        }
        y += rowH;
    }
    color_state(dev);
    return y;
}

// the moon : sweep the illuminated fraction New -> Full -> New via `t`, drawn with the widget's real moon
// renderer. The caller owns the texture handle + its cache key (so it can forget it on device-lost) ; we
// rebuild it only when the phase bucket / day tint changes. Returns the (localized) phase name.
// The 25 phase frames are BAKED ONCE into a horizontal strip (white -> tintable), then each frame just SAMPLES
// the current bucket via UV + tints by the day colour + mirrors for a waning moon. No per-frame texture create.
static const int MOON_F = 25;
const char* minimap_help_moon(u32 dev, u32& moonTex, int& moonKey, float cx, float cy, float r, float t, int lang) {
    // THE KEY IS THE PRODUCT, NOT THE ATTEMPT. This used to set moonKey = -2 ("baked") immediately after the
    // create call, without looking at what came back. A refused CreateTexture therefore left moonTex = 0 with
    // the key claiming success, so the very next frame re-entered and baked all 25 frames again: 102 400 pixels
    // of build_moon_argb, every frame, for as long as the refusal lasted, with nothing in the log. That is rule
    // 10 form C -- a partial success is not a success -- and a 1600-wide non-power-of-two texture is exactly the
    // kind of thing a driver refuses. Now: the key is only set when the handle EXISTS, the retry is bounded and
    // spaced, and the budget is re-armed by the caller's device-lost reset (it sets moonKey = -1, not -3).
    static const int      MOON_TRIES    = 6;
    static const unsigned MOON_RETRY_MS = 3000;
    static unsigned nextMs = 0;
    static int      tries  = 0;
    if (moonTex == 0) {
        if (moonKey != -3) { tries = 0; nextMs = 0; moonKey = -3; }   // fresh request, or the owner forgot the handle
        if (tries < MOON_TRIES && retry_due(nextMs)) {
            retry_arm(nextMs, MOON_RETRY_MS);
            ++tries;
            static u32 strip[64 * (MOON_F * 64)];                        // 64 tall x (25*64) wide, baked once
            static u32 tmp[64 * 64];
            for (int fi = 0; fi < MOON_F; ++fi) {
                build_moon_argb(tmp, 64, 0xFFFFFFFFu, (float)fi / (float)(MOON_F - 1), false);   // WHITE, waxing -> tint/mirror at draw
                for (int yy = 0; yy < 64; ++yy) for (int xx = 0; xx < 64; ++xx) strip[yy * (MOON_F * 64) + fi * 64 + xx] = tmp[yy * 64 + xx];
            }
            moonTex = make_texture_argb_mip(dev, MOON_F * 64, 64, strip);
            if (moonTex) {
                moonKey = -2;                                            // baked, and the handle is real
                if (tries > 1) windower::debug::log("minimap help: moon strip baked after %d attempt(s)", tries);
                tries = 0;
            } else if (tries >= MOON_TRIES) {
                windower::debug::log("minimap help: the %dx64 moon strip could not be created after %d attempts -- "
                                     "the Help tab's moon will not draw. A non-power-of-two %d-wide texture is the "
                                     "likely refusal ; everything else on that page is unaffected.",
                                     MOON_F * 64, tries, MOON_F * 64);
            }
        }
    }
    const float ph   = fmodf(t * 0.30f, 6.2831853f);
    const float frac = 0.5f - 0.5f * cosf(ph);                        // 0 (new) .. 1 (full) .. 0
    const bool  waning = (sinf(ph) < 0.0f);
    const int   di = ((int)(t * 0.12f)) & 7;                          // day tint (applied as the draw tint now)
    int bucket = (int)(frac * 24.0f + 0.5f); if (bucket < 0) bucket = 0; if (bucket > 24) bucket = 24;
    const float fw = 1.0f / (float)MOON_F;
    float u0 = (float)bucket * fw, u1 = u0 + fw;
    if (waning) { const float sw = u0; u0 = u1; u1 = sw; }            // mirror U -> waning (lit side flips)
    tex_state(dev, moonTex);
    tquad(dev, snap(cx - r), snap(cy - r), snap(2.0f * r), snap(2.0f * r), u0, u1, 0.0f, 1.0f, VANA_DAYCOL[di], VANA_DAYCOL[di]);
    dSetTex(dev, 0, 0);
    color_state(dev);
    rrect_stroke(dev, cx - r, cy - r, 2.0f * r, 2.0f * r, r, 0x40BECDFFu, 1.0f);   // faint rim (like draw_moon)
    // demo moon (Help / preview) : synthesize a 12-phase id from the illumination + waning flag
    const int id = waning ? (6 + (int)((1.0f - frac) * 6.0f + 0.5f)) % 12 : (int)(frac * 6.0f + 0.5f);
    return (lang == 1) ? moon_phase_name_fr(id) : moon_phase_name(id);
}

// the 8 elemental-day icons in a row from the atlas, each tinted by its VANA_DAYCOL, the active one (cycling
// via `t`) grown + glowing. Returns the (localized) name of the active day.
const char* minimap_help_day(u32 dev, Font* fo, u32 elemTex, float x, float cy, float t, int lang) {
    const int cur = ((int)(t * 0.5f)) & 7;                           // cycle the highlighted day
    const float cell = snap(26.0f), gap = snap(6.0f);
    if (elemTex) for (int i = 0; i < 8; ++i) {
        const float ix = x + i * (cell + gap), icx = ix + cell * 0.5f;
        const bool  on = (i == cur);
        const float sz = on ? cell : snap(cell * 0.80f);
        if (on) {                                                     // soft accent glow under the active day
            color_state(dev); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
            disc_glow(dev, icx, cy, cell * 0.62f, (VANA_DAYCOL[i] & 0x00FFFFFFu) | 0x66000000u, snap(6.0f));
            dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        tex_state(dev, elemTex);
        const u32 tint = on ? VANA_DAYCOL[i] : mm_darken(VANA_DAYCOL[i], 0.45f);
        tquad(dev, snap(icx - sz * 0.5f), snap(cy - sz * 0.5f), sz, sz, i / 8.0f, (i + 1) / 8.0f, 0.0f, 1.0f, tint, tint);
        dSetTex(dev, 0, 0);
    }
    color_state(dev);
    (void)fo;
    return (lang == 1) ? VANA_DAYNAME_FR[cur] : VANA_DAYNAME[cur];
}

} // namespace aio
