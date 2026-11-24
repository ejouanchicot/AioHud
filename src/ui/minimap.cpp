// minimap.cpp -- see minimap.h. Phase 1a placeholder : a square panel showing the 512px map bounds as a
// frame, with the player dot + heading arrow placed by the reversed transform. Validates the world->pixel
// math live before we extract the real zone map image (phase 1b).
#include "minimap.h"
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
    Font* r = f.fonts->get("Georgia", 700, t.italic);
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
static void dial_web(u32 dev, float ax, float ay, float aw, float bx, float by, float bw, float waist, float S,
                     u32 edgeCol = 0, u32 bodyCol = 0, bool filet = true) {
    if (!edgeCol) edgeCol = g_dm.edge;
    if (!bodyCol) bodyCol = g_dm.body;
    float dx = bx - ax, dy = by - ay; const float L = sqrtf(dx * dx + dy * dy); if (L < 1.0f) return; dx /= L; dy /= L;
    const float nx = -dy, ny = dx;
    const int N = 12;
    float lx[N + 1], ly[N + 1], rx[N + 1], ry[N + 1];
    for (int pass = 0; pass < 2; ++pass) {
        const float grow = pass == 0 ? 1.0f * S : 0.0f;                     // the dark edge is the brass, 1 px wider
        const u32 col = pass == 0 ? edgeCol : bodyCol;
        for (int i = 0; i <= N; ++i) {
            const float t = (float)i / (float)N, u = 1.0f - t;
            const float w = u * u * (aw + grow) + 2.0f * u * t * (waist + grow) + t * t * (bw + grow);   // the flank's half-width
            const float px = ax + dx * L * t, py = ay + dy * L * t;
            lx[i] = px + nx * w; ly[i] = py + ny * w; rx[i] = px - nx * w; ry[i] = py - ny * w;
        }
        for (int i = 0; i < N; ++i) {
            fill_tri(dev, lx[i], ly[i], lx[i + 1], ly[i + 1], rx[i + 1], ry[i + 1], col);
            fill_tri(dev, lx[i], ly[i], rx[i + 1], ry[i + 1], rx[i], ry[i], col);
        }
        for (int i = 0; i < N; ++i) {                                         // the anti-aliased silhouette
            seg_soft(dev, lx[i], ly[i], lx[i + 1], ly[i + 1], 1.1f, col);
            seg_soft(dev, rx[i], ry[i], rx[i + 1], ry[i + 1], 1.1f, col);
        }
    }
    if (!filet) return;
    const bool upIsL = ny < 0.0f;                                             // the flank facing up catches the light
    for (int i = 1; i < N - 1; ++i) {
        const float x0 = upIsL ? lx[i] : rx[i], y0 = upIsL ? ly[i] : ry[i], x1 = upIsL ? lx[i + 1] : rx[i + 1], y1 = upIsL ? ly[i + 1] : ry[i + 1];
        const float k = upIsL ? -1.0f : 1.0f;                                 // one px inside the flank
        seg_soft(dev, x0 + nx * k * 1.2f * S, y0 + ny * k * 1.2f * S, x1 + nx * k * 1.2f * S, y1 + ny * k * 1.2f * S, 0.8f * S, MA(0x80, g_dm.glint));
    }
}

// the four HORNS joining the corner subdials to the ring : drawn BEFORE the ring and the subdials, so both pieces sit
// on them and each horn comes out from under both -- one cast object, not a lug laid on top
static void draw_dial_horns(u32 dev, const UiConfig& c, float cx, float cy, float r, float S) {
    const float dk = 0.7071f, sr = snap(r * TUNE("mm.dial_sub_k", 0.165f)), rr = r + sr + TUNE("mm.dial_sub_gap", 1.5f) * S;
    const bool on[4] = { c.mmGrid != 0, c.mmClock && (c.mmClkTime || c.mmClkDay), c.mmClock && c.mmClkMoon, c.mmClock && (c.mmClkTime || c.mmClkDay) };   // TL TR BL BR
    static const float SX[4] = { -1.0f, 1.0f, -1.0f, 1.0f }, SY[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    color_state(dev);
    for (int i = 0; i < 4; ++i) {
        if (!on[i]) continue;
        const float x = cx + SX[i] * dk * rr, y = cy + SY[i] * dk * rr;
        const float dx = -SX[i] * dk, dy = -SY[i] * dk;                                         // toward the dial's centre
        dial_web(dev, x + dx * sr * 0.3f, y + dy * sr * 0.3f, sr * 0.62f, x + dx * (rr - r + 6.0f * S), y + dy * (rr - r + 6.0f * S), sr * 0.95f, sr * 0.42f, S);
    }
    color_state(dev);
}

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

static void draw_dial_instrument(u32 dev, const Frame& f, const UiConfig& c, const GameState& g, const VanaClock& v, u32 elemTex, u32 moonTex,
                                 float mapX, float mapY, float cx, float cy, float rc, float r, float S, float zoomA, bool sq = false) {
    (void)g;
    Font* tf = band_font(f, MM_TIME); Font* fo = f.font;
    const int di = (v.dayIdx < 0 || v.dayIdx > 7) ? 2 : v.dayIdx, ni = (di + 1) & 7;
    const float PI = 3.14159265f;
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
        // ---- TL : the map square ----
        if (c.mmGrid) {
            const float x = cx - off, y = cy - off;
            const int gc = ((int)floorf(mapX) - 16) / 32, gr = ((int)floorf(mapY) - 16) / 32;
            if (gc >= 0 && gc < 26 && gr >= 0 && gr < 99) {
                bezel(x, y, 0xFF17130Cu);
                for (int k = 1; k <= 2; ++k) { const float gr2 = sr * (0.45f + 0.25f * (float)k); color_state(dev); rrect_stroke(dev, x - gr2, y - gr2, 2.0f * gr2, 2.0f * gr2, gr2, 0x16F4E4B8u, 1.0f); }
                char gb[12]; sprintf(gb, "%c-%d", 'A' + gc, gr + 1);
                float gsz = snap(sr * 0.62f); if (tf) while (gsz > sr * 0.4f && tf->measure(gb, gsz) > sr * 1.6f) gsz -= 1.0f;
                engrave(x, y, gb, gsz, 0xFFF0D898u);
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
        // ---- TR : DAY / NIGHT, a small landscape : the sky's colour follows the hour smoothly (night blue, an orange dawn,
        //      a clear noon, dusk, night again) ; the sun and the moon ride an arc, rising on the left and setting on the
        //      right BEHIND brass hills ; the stars come out with the night ----
        if (c.mmClock && (c.mmClkTime || c.mmClkDay)) {
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
            u32 skyTop = mix(0xFF08112Au, 0xFF3C78CCu, dayK), skyBot = mix(0xFF1A2A56u, 0xFFA8D2F2u, dayK);
            skyTop = mix(skyTop, 0xFF3A3C7Cu, duskK * 0.5f); skyBot = mix(skyBot, 0xFFF0A050u, duskK * 0.85f);
            const float hz = y + sr * 0.22f;                                                         // the horizon line
            color_state(dev);
            const int NS = 16;                                                                       // the sky : strips with a vertical gradient
            for (int k = 0; k < NS; ++k) {
                const float ya = (y - sr) + (hz - (y - sr)) * (float)k / NS, yb = (y - sr) + (hz - (y - sr)) * (float)(k + 1) / NS;
                const float ym = (ya + yb) * 0.5f - y, hw = sqrtf(sr * sr - ym * ym > 0.0f ? sr * sr - ym * ym : 0.0f);
                const float ka = (ya - (y - sr)) / (hz - (y - sr)), kb = (yb - (y - sr)) / (hz - (y - sr));
                const u32 ca2 = mix(skyTop, skyBot, ka * ka), cb2 = mix(skyTop, skyBot, kb * kb);
                grad_quad(dev, x - hw, ya, 2.0f * hw, yb - ya + 0.6f, ca2, ca2, cb2, cb2);
            }
            static const float STX[9] = { -0.55f, -0.2f, 0.15f, 0.5f, -0.38f, 0.32f, 0.0f, -0.68f, 0.66f }, STY[9] = { -0.62f, -0.78f, -0.55f, -0.70f, -0.30f, -0.25f, -0.88f, -0.05f, -0.12f };
            const float starA = (1.0f - dayK) * (1.0f - duskK * 0.6f);
            for (int k = 0; k < 9; ++k)
                if (starA > 0.02f) disc(dev, snap(x + STX[k] * sr), snap(y + STY[k] * sr), (k % 3 == 0) ? 0.9f : 0.6f, ((u32)(starA * 230.0f) << 24) | 0x00FFF6DCu);
            // the sun and the moon on their arc (left horizon -> top -> right horizon), hidden by the hills below
            const float AR = sr * 0.66f;
            for (int body = 0; body < 2; ++body) {
                const float tt = body == 0 ? t : fmodf(t + 12.0f, 24.0f);                             // the moon is opposite the sun
                const float ph = (tt - 6.0f) / 12.0f * PI;                                            // 0 at its rise, pi at its set
                const float bx = x - cosf(ph) * AR, by = hz - sinf(ph) * AR * 0.95f;
                if (by > hz + sr * 0.3f) continue;                                                    // well under the hills
                if (body == 0) {
                    const float sR = sr * 0.17f;
                    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
                    disc_glow(dev, bx, by, sR, 0x80FFE8A0u, sR * 1.6f);
                    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                    disc(dev, bx, by, sR + 0.6f, 0xFFB88A30u); disc(dev, bx, by, sR, 0xFFFFD670u);
                    disc(dev, bx - sR * 0.3f, by - sR * 0.3f, sR * 0.35f, 0x90FFFFFFu);
                } else {
                    draw_moon(dev, snap(bx), snap(by), sr * 0.17f, moonTex);
                    color_state(dev);
                }
            }
            // the GROUND, engraved in depth : a farC hill tinted by the sky (haze by day, dark by night) with two small pines
            // on its crest ; the near hill in brass, lit on its crest and darker toward the foot ; engraved hatching below
            const int NH = 22;
            auto ridge = [&](float xn, int layer) {                                                 // the crest height (fraction of sr)
                return layer == 0 ? 0.20f + 0.08f * sinf(xn * 2.3f + 2.1f) + 0.04f * sinf(xn * 6.1f + 0.4f)
                                  : 0.08f + 0.07f * sinf(xn * 3.2f + 0.7f) + 0.035f * sinf(xn * 7.9f + 1.3f);
            };
            for (int layer = 0; layer < 2; ++layer) {
                float hx[NH + 1], hy[NH + 1], hb[NH + 1];
                for (int i = 0; i <= NH; ++i) {
                    const float xn = -1.0f + 2.0f * (float)i / NH, dx = xn * sr;
                    const float circ = sqrtf(sr * sr - dx * dx > 0.0f ? sr * sr - dx * dx : 0.0f);
                    float sy = hz - sr * ridge(xn, layer); if (sy < y - circ + 1.0f) sy = y - circ + 1.0f;
                    hx[i] = x + dx; hy[i] = sy; hb[i] = y + circ;
                }
                const u32 farC = mix(skyBot, 0xFF2A2418u, 0.55f + 0.35f * (1.0f - dayK));             // the farC hill : the sky's haze over dark
                for (int i = 0; i < NH; ++i) {
                    if (layer == 0) {
                        fill_tri(dev, hx[i], hy[i], hx[i + 1], hy[i + 1], hx[i + 1], hb[i + 1], farC);
                        fill_tri(dev, hx[i], hy[i], hx[i + 1], hb[i + 1], hx[i], hb[i], farC);
                    } else {                                                                        // the near hill : three bands, lit at the crest
                        const float d1 = sr * 0.07f, d2 = sr * 0.20f;
                        const u32 c0 = g_dm.body, c1 = g_dm.bot, c2 = g_dm.dark;
                        fill_tri(dev, hx[i], hy[i], hx[i + 1], hy[i + 1], hx[i + 1], hy[i + 1] + d1, c0);
                        fill_tri(dev, hx[i], hy[i], hx[i + 1], hy[i + 1] + d1, hx[i], hy[i] + d1, c0);
                        fill_tri(dev, hx[i], hy[i] + d1, hx[i + 1], hy[i + 1] + d1, hx[i + 1], hy[i + 1] + d2, c1);
                        fill_tri(dev, hx[i], hy[i] + d1, hx[i + 1], hy[i + 1] + d2, hx[i], hy[i] + d2, c1);
                        fill_tri(dev, hx[i], hy[i] + d2, hx[i + 1], hy[i + 1] + d2, hx[i + 1], hb[i + 1], c2);
                        fill_tri(dev, hx[i], hy[i] + d2, hx[i + 1], hb[i + 1], hx[i], hb[i], c2);
                    }
                }
                for (int i = 0; i < NH; ++i) seg_soft(dev, hx[i], hy[i], hx[i + 1], hy[i + 1], 1.2f, layer == 0 ? farC : g_dm.body);   // AA crests
                if (layer == 0) {                                                                   // two small pines on the farC crest
                    static const float PX[2] = { -0.42f, -0.28f }, PH[2] = { 0.16f, 0.12f };
                    for (int k = 0; k < 2; ++k) {
                        const float px2 = x + PX[k] * sr, py2 = hz - sr * ridge(PX[k], 0) + 0.5f, ph2 = sr * PH[k], pw = ph2 * 0.38f;
                        fill_tri(dev, px2, py2 - ph2, px2 + pw, py2, px2 - pw, py2, mix(farC, 0xFF141008u, 0.5f));
                    }
                } else {
                    for (int i = 0; i < NH; ++i)                                                    // the light on the near crest
                        seg_soft(dev, hx[i], hy[i] + 0.8f, hx[i + 1], hy[i + 1] + 0.8f, 0.8f * S, MA(0xA0, g_dm.glint));
                    for (int k = 0; k < 7; ++k) {                                                   // engraved hatching on the ground
                        const float gx = x - sr * 0.6f + sr * 0.2f * (float)k, gy = hz + sr * 0.32f + (float)(k & 1) * sr * 0.12f;
                        const float dxh = gx - x, lim = sqrtf(sr * sr - dxh * dxh > 0.0f ? sr * sr - dxh * dxh : 0.0f);
                        if (gy + sr * 0.18f > y + lim - 1.0f) continue;
                        seg_soft(dev, gx, gy, gx + sr * 0.12f, gy + sr * 0.18f, 0.8f, MA(0x60, g_dm.shade));
                    }
                }
            }
            ring_sector(dev, x, y, sr - 0.8f, sr + 0.4f, 0.0f, 2.0f * PI, g_dm.dark);              // a clean rim over the stair-stepped strips
            color_state(dev);
        }
        // ---- the BAND : ONE brass bar under the ring, from the moon subdial to the day subdial (both sit on its ends ; it
        //      touches the ring at the S). Two compartments, each under its subdial and lined with ITS colour -- night blue for
        //      the moon, the element's colour for the day -- a gilt cabochon between them : which line is whose reads at once ----
        const bool bandMoon = c.mmClock && c.mmClkMoon, bandDay = c.mmClock && (c.mmClkTime || c.mmClkDay);
        const float bandH = snap(pSz + 12.0f * S), by0 = snap(sq ? cy + off + sr * 0.95f : cy + r - 1.0f * S), bmid = by0 + bandH * 0.5f + 0.5f * S;   // the square : under its corner subdials, as on the round
        const float bx0 = snap(sq ? cx - r : cx - off - sr * 0.8f - TUNE("mm.dial_fillet", 12.0f) * S - 3.0f * S), bx1 = snap(sq ? cx + r : cx + off + sr * 0.8f + TUNE("mm.dial_fillet", 12.0f) * S + 3.0f * S), bins = 2.6f * S;   // the square : the frame's width   // both fillets land on the band
        const float lc0 = bx0 + bins, lc1 = cx - 7.0f * S, rc0 = cx + 7.0f * S, rc1 = bx1 - bins;   // the two compartments
        if (bandMoon || bandDay) {
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
                    for (int i = 0; i < N; ++i) fill_tri(dev, Cx, Cy, px[i], py[i], px[i + 1], py[i + 1], col);
                    for (int i = 0; i < N; ++i) seg_soft(dev, px[i], py[i], px[i + 1], py[i + 1], 1.0f, col);   // its AA silhouette
                }
            };
            const float filR = TUNE("mm.dial_fillet", 12.0f) * S;
            for (int k = 0; k < 2; ++k) {                                                        // the dark : subdial rims + fillet rims
                if (k == 0 ? !bandMoon : !bandDay) continue;
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
                if (k == 0 ? !bandMoon : !bandDay) continue;
                fillet(k == 0 ? cx - off : cx + off, cy + off, sr + 1.6f * S, by0, filR, g_dm.body);
            }
            grad_quad(dev, bx0 + rad, by0 + bandH - 1.0f, bx1 - bx0 - 2.0f * rad, 1.0f, MA(0x70, g_dm.shade), MA(0x70, g_dm.shade), MA(0x70, g_dm.shade), MA(0x70, g_dm.shade));
            for (int k = 0; k < 2; ++k) {
                if (k == 0 ? !bandMoon : !bandDay) continue;
                const float a0 = k == 0 ? lc0 : rc0, a1 = k == 0 ? lc1 : rc1, iy = by0 + bins;
                rrect(dev, a0 - 0.8f * S, iy - 0.8f * S, a1 - a0 + 1.6f * S, bandH - 2.0f * bins + 1.6f * S, snap(1.8f * S), g_dm.dark, g_dm.lite);   // the chamfer
                rrect(dev, a0, iy, a1 - a0, bandH - 2.0f * bins, snap(1.4f * S), 0xFF0F0C08u, 0xFF1E1810u);                                         // the recess
            }
            disc(dev, cx, bmid, 4.2f * S, g_dm.edge); disc(dev, cx, bmid, 3.4f * S, g_dm.lite);    // the gilt cabochon
            disc(dev, cx - 1.0f * S, bmid - 1.0f * S, 1.2f * S, 0xC0FFF6DCu);
            color_state(dev);
        }
        // ---- BL : the MOON, large, in a starry night ; its line on the band : the next event, in words ----
        if (c.mmClock && c.mmClkMoon) {
            const float x = cx - off, y = cy + off;
            const bool fullFirst = v.daysToFull < v.daysToNew;
            const int dd = fullFirst ? v.daysToFull : v.daysToNew;
            char mb[32];
            if (dd <= 0) sprintf(mb, "%s", c.lang == 1 ? (fullFirst ? "Pleine" : "Nouvelle") : (fullFirst ? "Full" : "New"));
            else sprintf(mb, c.lang == 1 ? (fullFirst ? "Pleine %dj" : "Nouv. %dj") : (fullFirst ? "Full %dd" : "New %dd"), dd);
            const float ty = bmid;
            bezel(x, y, 0xFF101C3Cu, true);
            color_state(dev);
            static const float ST[6][2] = { {-0.62f,-0.30f}, {0.58f,-0.48f}, {-0.35f,-0.70f}, {0.70f,0.10f}, {-0.72f,0.22f}, {0.30f,-0.75f} };
            for (int k = 0; k < 6; ++k) disc(dev, snap(x + ST[k][0] * sr), snap(y + ST[k][1] * sr), 0.7f, 0xB0FFF6DCu);
            draw_moon(dev, snap(x), snap(y), sr * 0.62f, moonTex);
            color_state(dev);
            plateText((lc0 + lc1) * 0.5f, ty, mb, fullFirst ? 0xFFF8E6B0u : 0xFFC8D6F4u, -1);
        }
        // ---- BR : the DAY -- the element of the day, large, on an enamel tinted with its colour ; its plate : the exact
        //      time, then a chevron and tomorrow's element ----
        if (c.mmClock && (c.mmClkTime || c.mmClkDay)) {
            const float x = cx + off, y = cy + off;
            char tb[16]; if (c.mmClkTime) sprintf(tb, "%02d:%02d \xE2\x80\xBA", v.hh, v.mm); else sprintf(tb, "\xE2\x80\xBA");
            const float ty = bmid;
            const u32 dc = VANA_DAYCOL[di];
            const u32 enamel = 0xFF000000u | ((((dc >> 16) & 0xFF) * 30 / 100) << 16) | ((((dc >> 8) & 0xFF) * 30 / 100) << 8) | (((dc & 0xFF) * 30 / 100));
            bezel(x, y, enamel, true);
            color_state(dev);
            dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
            disc_glow(dev, x, y, sr * 0.35f, (dc & 0x00FFFFFFu) | 0x50000000u, sr * 0.55f);           // the element glows on its enamel
            dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            if (elemTex) {
                const float isz = sr * 1.25f;
                tex_state(dev, elemTex);
                tquad(dev, snap(x - isz * 0.5f), snap(y - isz * 0.5f), isz, isz, di / 8.0f, (di + 1) / 8.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
                dSetTex(dev, 0, 0);
            }
            plateText((rc0 + rc1) * 0.5f, ty, tb, 0xFFF4EEDFu, (c.mmClkDay && elemTex) ? ni : -1);
        }
        color_state(dev);
    }
    // -- the cardinal letters, engraved in the brass band -- LAST : nothing of the case may cover them --
    if (fo) {
        const float rm = (rc + r) * 0.5f, csz = snap(TUNE("mm.dial_card", 13.0f) * S * clampf(c.mmCardSz, 0.5f, 2.0f) / 0.7f);
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

void Minimap::on_device_lost() { for (int i = 0; i < AS_N; ++i) { astG_[i] = 0; astGR_[i] = TexRetry{}; } s_shAtlas = 0; for (int i = 0; i < AS_N; ++i) s_shAst[i] = 0; mkAtlas_ = 0; mkAtlasR_ = TexRetry{}; for (int i = 0; i < AS_N; ++i) { ast_[i] = 0; astR_[i] = TexRetry{}; } mapTex_ = 0; mapFileId_ = 0; mkPlayer_ = 0; mkMob_ = 0; elemTex_ = 0; moonTex_ = 0; moonKey_ = -1; mkTries_ = 0; mkNextMs_ = 0; }   // FORGET handles (+ re-arm the retry budget)
void Minimap::dispose() {
    s_shAtlas = 0; for (int i = 0; i < AS_N; ++i) s_shAst[i] = 0;
    for (int i = 0; i < AS_N; ++i) { release_texture(astG_[i]); astG_[i] = 0; astGR_[i] = TexRetry{}; }
    release_texture(mkAtlas_); mkAtlas_ = 0; mkAtlasR_ = TexRetry{};
    for (int i = 0; i < AS_N; ++i) { release_texture(ast_[i]); ast_[i] = 0; astR_[i] = TexRetry{}; }
    release_texture(mapTex_); mapTex_ = 0; mapFileId_ = 0;
    release_texture(mkPlayer_); release_texture(mkMob_); release_texture(elemTex_); release_texture(moonTex_);
    mkPlayer_ = 0; mkMob_ = 0; elemTex_ = 0; moonTex_ = 0; moonKey_ = -1; mkTries_ = 0; mkNextMs_ = 0;
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

    // ---- the map grid square, "(F-10)" -- the native compass's line, the client's own formula (FUN_05538C10) :
    //      column 'A' + (px - 16) / 32, row 1 + (py - 16) / 32 on the 512 px native map, C division (toward zero) ----
    if (astral) {                                              // the ring, its graduations, the glint, the cardinal medallions
        const bool grey = c.mmFrame != 0 && astG_[AS_BASE] && astG_[AS_TICKS] && astG_[AS_GLINT];   // the theme's metal : the grey kit, tinted
        const u32 tint = grey ? dm_toward(g_dm.body, 0xFFFFFFFFu, 0.35f) : 0xFFFFFFFFu;
        if (dial) draw_dial_horns(dev, c, ccx, ccy, r, S);         // under the ring and the subdials : one cast object
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
    if (dial) {
        static float s_lastZoom = 0.0f, s_zoomT = -10.0f;                  // the zoom readout lives 1.6 s after a change
        if (c.mmZoom != s_lastZoom) { if (s_lastZoom != 0.0f) s_zoomT = f.t; s_lastZoom = c.mmZoom; }
        float za = 1.0f - (f.t - s_zoomT - 1.0f) / 0.6f; za = clampf(za, 0.0f, 1.0f);
        if (f.t < s_zoomT) za = 0.0f;                                       // f.t wrapped
        const VanaClock dv = dial_vana(g.vana);
        if (dialSq) { color_state(dev); rrect_stroke(dev, wx0 - 0.5f, wy0 - 0.5f, wx1 - wx0 + 1.0f, wy1 - wy0 + 1.0f, snap(2.0f * S), 0xC0140F08u, snap(1.4f * S)); }   // the inner edge's shadow on the map
        draw_dial_instrument(dev, f, c, g, dv, elemTex_, moonTex_, mapX, mapY, ccx, ccy, dialSq ? r - fi : rc, r, S, za, dialSq);
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
