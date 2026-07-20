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
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/texture.h"             // make_texture_argb / release_texture
#include "gfx/window.h"              // window_theme_family / window_theme_variant / box_hue_color (frame theme)
#include "gfx/d3d.h"                 // dSet* + FVF constants
#include "ui/text_style.h"           // te_sz/te_ow/te_col : shared TextStyle-resolve impl
#include "ui/entity_color.h"         // in_my_group / allegiance_color : shared claim/allegiance palette (with the Target box)
#include "ui/box_style.h"            // draw_themed_box : shared themed chrome for the clock box
#include <math.h>
#include <stdio.h>
#include <time.h>

namespace aio {

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// darken a colour toward black by fraction f (0..1), keeping its alpha -> a crisp AA outline for the round rim.
static inline u32 mm_darken(u32 c, float f) {
    const int rr = (int)(((c >> 16) & 0xFF) * (1.0f - f)), gg = (int)(((c >> 8) & 0xFF) * (1.0f - f)), bb = (int)((c & 0xFF) * (1.0f - f));
    return (c & 0xFF000000u) | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
}

// marker icons (white, tinted per entity) : Location pin (points DOWN) for the player, Arrow (points UP) for mobs.
static const char* MK_PLAYER_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\icon_marker_player.raw"); return b; }
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
    return allegiance_color(me.type == 3, me.type == 1, me.id, me.claimId, me.status, me.pflags, 0xFFF2F2B7u);
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

void Minimap::on_device_lost() { mapTex_ = 0; mapFileId_ = 0; mkPlayer_ = 0; mkMob_ = 0; elemTex_ = 0; moonTex_ = 0; moonKey_ = -1; mkTried_ = false; }   // FORGET handles
void Minimap::dispose() {
    release_texture(mapTex_); mapTex_ = 0; mapFileId_ = 0;
    release_texture(mkPlayer_); release_texture(mkMob_); release_texture(elemTex_); release_texture(moonTex_);
    mkPlayer_ = 0; mkMob_ = 0; elemTex_ = 0; moonTex_ = 0; moonKey_ = -1; mkTried_ = false;
}

void Minimap::draw(const Frame& f) {
    ui_config().mmVisible = false;                              // cleared unless we render below (gates the wheel-zoom)
    if (!visible_ || !f.game) return;
    const GameState& g = *f.game;
    const UiConfig& c = ui_config();
    if (!c.mmShow || !g.inGame) return;                         // phase 1a : live only (needs a real map record)
    u32 dev = f.dev;
    if (!mkTried_) { mkPlayer_ = load_raw_texture_mip(dev, MK_PLAYER_PATH(), 64, 64); mkMob_ = load_raw_texture_mip(dev, MK_MOB_PATH(), 64, 64);
                     elemTex_ = load_raw_texture_mip(dev, ELEM_ATLAS_PATH(), 256, 32); mkTried_ = true; }

    const float S = scale_ * clampf(c.mmScale, 0.5f, 2.0f);     // overall box / header scale
    const float mapD = snap(220.0f * S * clampf(c.mmMapSize, 0.5f, 1.6f));   // map DIAMETER -- independent of the box
    const float bm = c.mmClock ? snap(5.0f * S) : 0.0f;         // inner margin around the map (inside the themed box)
    const float hdrW = snap(210.0f * S);                        // the clock header's natural width
    const float Hh = clock_header_height(c, S);                 // clock header height (enabled rows only ; 0 if off)
    const bool  hasClock = (c.mmClock != 0);                    // clock header shown at all
    const int   cpos = hasClock ? ((c.mmClockPos < 0 || c.mmClockPos > 3) ? 0 : c.mmClockPos) : -1;   // header placement (-1 = none)
    // header BAND width : TOP/BOTTOM keep the natural hdrW (centred over the map) ; LEFT/RIGHT hug the actual
    // content so the header<->map gap equals the TOP margin (bm) instead of hdrW's empty side padding.
    float hbw = hdrW;
    if (cpos == 2 || cpos == 3) { float cw = clock_header_width(f, g.vana, c, elemTex_, S); if (cw > snap(20.0f * S)) hbw = cw + snap(6.0f * S); }
    float Wbox, boxH;
    if (cpos == 0 || cpos == 1) {                               // TOP / BOTTOM : header stacked with the map
        const float needW = mapD + 2.0f * bm;                  // width the map + side margins want
        Wbox = (needW > hbw ? needW : hbw);                   // box width = max(header, map+margins)
        boxH = Hh + mapD + 2.0f * bm;                          // header + gap + map + margins
    } else if (cpos == 2 || cpos == 3) {                       // LEFT / RIGHT : header beside the map
        Wbox = hbw + mapD + 2.0f * bm;                        // header (hugged) + gap(bm) + map + outer bm  == TOP margin
        boxH = (Hh > mapD ? Hh : mapD) + 2.0f * bm;            // box height = max(header, map) + margins
    } else {                                                   // no clock : the box is just the map
        Wbox = mapD;
        boxH = mapD;
    }

    // ---- position : user fraction wins over the layout default (px,py = BOX top-left) ----
    float px, py;
    if (c.mmPosSet && f.screenW > 0.0f && f.screenH > 0.0f) { px = snap(c.mmX * f.screenW); py = snap(c.mmY * f.screenH); }
    else { px = snap(px_); py = snap(py_); }

    // ---- EDIT MODE : drag the WHOLE box (header + map) ----
    int dch = 0, dcv = 0;
    if (c.editLayout &&
        edit_box_drag(g_mmEdit, EDITBOX_MINIMAP, f, px, py, Wbox, boxH, ZPERM_HUB,
                      ui_config().mmPosSet, ui_config().mmX, ui_config().mmY, dch, dcv, ui_config().mmScale))
        edit_box_grid(dev, f, g_mmEdit, px, py, Wbox, boxH, dch != 0, dcv != 0);   // highlight the centre axis when snapped

    // ---- Vana'diel clock box : the box background FOLLOWS the party/alliance box theme (like every other box),
    // with the clock header on top ; the map fills the region below. Drawn FIRST (behind the map). ----
    const float boxPy = py;
    // ---- header top-left (hx,hy) and MAP CENTRE (ccx,ccy) depend on the clock placement (mmClockPos) ----
    const float r = mapD * 0.5f;
    float hx = px, hy = py, ccx, ccy;
    if (cpos == 0) {                                            // TOP : header above the map
        hx = px + (Wbox - hbw) * 0.5f;  hy = py;
        ccx = px + Wbox * 0.5f;         ccy = py + Hh + bm + r;
    } else if (cpos == 1) {                                    // BOTTOM : header below the map
        ccx = px + Wbox * 0.5f;         ccy = py + bm + r;
        // subtract the header's intrinsic top lead (draw_clock_header starts at y+6S) so the map<->clock gap == bm
        // (the TOP reference) ; the freed 6S lands under the header, mirroring TOP's top pad.
        hx = px + (Wbox - hbw) * 0.5f;  hy = py + bm + mapD + bm - snap(6.0f * S);
    } else if (cpos == 2) {                                    // LEFT : header flush left, gap(bm), map
        hx = px;                        hy = py + (boxH - Hh) * 0.5f;
        ccx = px + hbw + bm + r;        ccy = py + boxH * 0.5f;
    } else if (cpos == 3) {                                    // RIGHT : header right of the map
        ccx = px + bm + r;              ccy = py + boxH * 0.5f;
        hx = px + bm + mapD + bm;       hy = py + (boxH - Hh) * 0.5f;
    } else {                                                   // no clock : the map fills the box
        ccx = px + Wbox * 0.5f;         ccy = py + boxH * 0.5f;
    }
    const float mapLeft = ccx - r, mapTop = ccy - r;           // map region top-left
    if (hasClock) {
        // rebuild the supersampled moon texture only when the day/phase changes (cheap otherwise).
        if (c.mmClkMoon) {
            const int di = (g.vana.dayIdx < 0 || g.vana.dayIdx > 7) ? 2 : g.vana.dayIdx;
            const int mkey = di * 1000 + g.vana.moonPct * 4 + (g.vana.waning ? 2 : 0);
            if (mkey != moonKey_) {
                if (moonTex_) { release_texture(moonTex_); moonTex_ = 0; }
                static u32 moonBuf[64 * 64];
                build_moon_argb(moonBuf, 64, VANA_DAYCOL[di], g.vana.moonPct / 100.0f, g.vana.waning);
                moonTex_ = make_texture_argb_mip(dev, 64, 64, moonBuf);
                moonKey_ = mkey;
            }
        }
        color_state(dev);
        draw_themed_box(dev, f.skin, px, boxPy, Wbox, boxH, c.mmBox, 1.0f, S);   // shared themed chrome (bg + border + transparency + Same-as-Party / own theme)
        draw_clock_header(dev, f, g.vana, c, elemTex_, moonTex_, hx, hy, hbw, S);
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
        mapRetries_ = 12; mapRetryAt_ = 0;                     // fresh load budget, try immediately
    }
    if (mapTex_ == 0 && g.map.fileId && mapRetries_ > 0) {     // not loaded yet -> (re)try, throttled
        const unsigned nowMs = GetTickCount();
        if ((int)(nowMs - mapRetryAt_) >= 0) {
            u32* pixels = 0; int mw = 0, mh = 0; MapLoadDiag md;
            // Only STOP retrying when the texture actually exists. This used to clear the budget as soon as the DAT
            // DECODED, without checking make_texture_argb_mip -- so a decode that succeeded followed by a failed
            // texture create left the map black forever with retries spent and NOTHING logged (the failure log lives
            // in the else branch). That is the black-map-on-a-valid-record case: file fine, texture missing.
            if (load_zone_map(g.map.fileId, pixels, mw, mh, &md)) {
                mapTex_ = make_texture_argb_mip(dev, mw, mh, pixels); mapW_ = mw; mapH_ = mh; free_map_image(pixels);
                if (mapTex_) mapRetries_ = 0;
                else {
                    --mapRetries_; mapRetryAt_ = nowMs + (mapRetries_ > 8 ? 0u : 300u);   // first tries NEXT FRAME : a CreateTexture miss right after a zone-in recovers at once, so the map no longer flashes black for 300 ms
                    if (mapRetries_ == 0)
                        windower::debug::log("MAP FAIL zone=%u fileId=0x%04X : DAT decoded fine (%dx%d) but CreateTexture failed -- image too large for the device, or out of video memory",
                                             g.map.zone, g.map.fileId, mw, mh);
                }
            }
            else {
                --mapRetries_; mapRetryAt_ = nowMs + (mapRetries_ > 8 ? 0u : 300u);   // first 3 next-frame (kills the black flash), then 300 ms apart (bounded)
                // ALWAYS-ON failure log (not behind a command) : a black minimap is rare and unpredictable, so
                // a probe you must remember to arm would miss the occurrence. One line per zone, only on the
                // LAST retry -- by then it is a real failure, not the normal not-ready-yet right after a zone-in.
                if (mapRetries_ == 0) {
                    static const char* STEP[] = { "OK", "NO FFXI ROOT", "PATH UNRESOLVED", "FILE UNREADABLE", "NO GRAPHIC CHUNK", "FORMAT REJECTED" };
                    windower::debug::log("MAP FAIL zone=%u submap=%d valid=%d flags=0x%04X fileIdx=%u fileId=0x%04X scale=%d off=(%d,%d)",
                                         g.map.zone, current_submap(), g.map.valid ? 1 : 0, g.map.flags,
                                         g.map.fileIdx, g.map.fileId, g.map.scale, g.map.offX, g.map.offY);
                    windower::debug::log("MAP   step=%s  overlay=%d  size=%u  chunkTypes=0x%08X  dims=%dx%d  fmtFlags=0x%02X",
                                         STEP[md.step & 7], md.overlay ? 1 : 0, md.fileSize, md.chunkTypes, md.W, md.H, md.fmtFlags);
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
    if (!mapTex_ && mapRetries_ > 0) return;

    // BLACK-MAP catch-all. The block above only logs after its 12 retries are spent, and it only RUNS when
    // g.map.fileId is non-zero -- so a record that is "valid" with fileId 0 (or one whose retries were never
    // armed) left the map black with NOTHING in the log. That blind spot is exactly what the first capture hit.
    // One line per zone, only once the texture is genuinely absent and no retry budget is left.
    if (!mapTex_) {
        static unsigned lastLoggedZone = 0xFFFFFFFFu;
        if (g.map.zone != lastLoggedZone && mapRetries_ <= 0) {
            lastLoggedZone = g.map.zone;
            windower::debug::log("MAP BLACK zone=%u submap=%d valid=%d fileId=0x%04X fileIdx=%u flags=0x%04X scale=%d retries=%d",
                                 g.map.zone, current_submap(), g.map.valid ? 1 : 0, g.map.fileId, g.map.fileIdx,
                                 g.map.flags, g.map.scale, mapRetries_);
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
    if (c.mmFrame == 1 && !round) {
        const int theme = c.skinTheme; const float lum = c.skinLum;
        if (window_theme_is_proc(theme)) { draw_proc_window(dev, theme, mapLeft, mapTop, mapD, mapD, 0xFFFFFFFFu, false, true, lum, c.skinHue); fi = snap(6.0f * S); themedOk = true; }
        else if (f.skin && f.skin->ready()) { draw_window(dev, *f.skin, mapLeft, mapTop, mapD, mapD, 0xFFFFFFFFu, S, false, true); fi = snap(15.0f * S); themedOk = true; }
    }
    const float wx0 = mapLeft + fi, wy0 = mapTop + fi, wx1 = mapLeft + mapD - fi, wy1 = mapTop + mapD - fi;   // clip rect (inset past the chrome border)

    // ---- ROUND border : the 9-slice chrome can't wrap a circle, so render the theme's (or custom) border colour
    // as a crisp solid ring -- a thin dark AA outline + the colour band -- and inset the circular clip so the map
    // fills only the interior, leaving the ring visible. Gives the round minimap a real border, not a soft glow. ----
    float rc = r;                                            // circle clip radius = the interior map area (inside the ring)
    if (round && c.mmClock && c.mmBezel) {
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
            const float rr = clampf(5.0f * S, 4.0f, 10.0f);
            rrect_stroke(dev, sx - rr, sy - rr, 2.0f * rr, 2.0f * rr, rr, 0x88000000u, snap(3.0f * S));   // dark rim
            rrect_stroke(dev, sx - rr, sy - rr, 2.0f * rr, 2.0f * rr, rr, lc, snap(1.8f * S));            // colour ring
            disc(dev, snap(sx), snap(sy), snap(1.6f * S), lc);                                            // centre pip
        }
    }

    // ---- mob markers : the Arrow icon rotated by heading, tinted by claim colour ----
    if (g.mapEntN > 0 && mkMob_ && c.mmMob) {
        const float mkSz = clampf(2.4f + 1.0f * Z, 4.4f, 11.0f) * S * MSc;
        tex_state(dev, mkMob_);
        for (int e = 0; e < g.mapEntN; ++e) {
            const MapEntity& me = g.mapEnts[e];
            if (me.type != 3) continue;
            const float ex = (g.map.scale * me.x) / 5.0f - (float)g.map.offX;
            const float ey = (g.map.scale * -me.z) / 5.0f - (float)g.map.offY;
            const float sx = ccx + (ex - mapX) * pn, sy = ccy + (ey - mapY) * pn;
            if (round) { const float dx = sx - ccx, dy = sy - ccy, lim = rc - mkSz * 0.61f; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }   // keep the whole marker inside the disc
            else if (sx < wx0 || sx > wx1 || sy < wy0 || sy > wy1) continue;
            const float ang = eased_angle(me.id, me.heading + 1.5708f);
            tquad_rot(dev, snap(sx), snap(sy), mkSz * 1.22f, mkSz * 1.22f, ang, 0x99000000u);   // dark outline
            tquad_rot(dev, snap(sx), snap(sy), mkSz, mkSz, ang, mm_ent_color(me));              // colour fill
        }
        dSetTex(dev, 0, 0);
    }

    // ---- NPC / other-PC markers : a tinted dot, per-type visibility toggle ----
    if (g.mapEntN > 0 && (c.mmNPC || c.mmPC)) {
        color_state(dev);
        const float dF = clampf(0.9f + 0.42f * Z, 1.5f, 4.2f) * S * MSc, dO = dF + 0.5f * S;   // NPC / PC pastilles -- smaller
        for (int e = 0; e < g.mapEntN; ++e) {
            const MapEntity& me = g.mapEnts[e];
            if (me.type == 3) continue;
            if (me.type == 2 ? !c.mmNPC : !c.mmPC) continue;               // 2 = NPC, else PC
            const float ex = (g.map.scale * me.x) / 5.0f - (float)g.map.offX;
            const float ey = (g.map.scale * -me.z) / 5.0f - (float)g.map.offY;
            const float sx = ccx + (ex - mapX) * pn, sy = ccy + (ey - mapY) * pn;
            if (round) { const float dx = sx - ccx, dy = sy - ccy, lim = rc - dO; if (lim < 0.0f || dx * dx + dy * dy > lim * lim) continue; }   // keep the whole dot inside the disc
            else if (sx < wx0 || sx > wx1 || sy < wy0 || sy > wy1) continue;
            disc(dev, snap(sx), snap(sy), dO, 0x88000000u);
            disc(dev, snap(sx), snap(sy), dF, mm_ent_color(me));
        }
    }

    // ---- player marker : the Location pin at the centre, rotated to face the heading, navy + halo ----
    if (mkPlayer_) {
        const float pSz = clampf(3.2f + 1.3f * Z, 8.0f, 15.0f) * S * MSc;
        const float pang = g.meHeading + 1.5708f;
        tex_state(dev, mkPlayer_);
        tquad_rot(dev, snap(ccx), snap(ccy), pSz * 1.2f, pSz * 1.2f, pang, 0xF0FFFFFFu);   // white halo behind
        tquad_rot(dev, snap(ccx), snap(ccy), pSz,        pSz,        pang, 0xFF12224Au);    // navy pin
        dSetTex(dev, 0, 0);
    }

    // ---- SQUARE frame stroke on top : custom colour, or the fallback when an FFXI skin wasn't ready yet ----
    // (round is drawn as a solid ring BEFORE the map above ; a real themed square chrome sets themedOk.)
    if (!themedOk && !round && c.mmFrame != 0) {
        const u32 fc = mm_frame_color(c, f);
        color_state(dev);
        const float sqbw = snap(1.6f * S * clampf(c.mmSqBorder, 0.5f, 2.0f));   // Square border-width slider
        rrect_stroke(dev, mapLeft - sqbw, mapTop - sqbw, mapD + 2.0f * sqbw, mapD + 2.0f * sqbw, snap(3.0f * S) + sqbw, fc, sqbw);   // grow the border OUTWARD from the map edge (was drawn inward -> it ate into the map)
    }
}

// ================= Help live samples : draw the REAL minimap elements (SAME colours / marker draw as the
// widget) for the Help tab (config_page.cpp). Everything below reuses the widget's marker/moon/day renderers
// so the sample and the live minimap can never drift. No game memory is touched -- the data is synthetic.

void minimap_help_textures(u32 dev, u32& mkPlayer, u32& mkMob, u32& elemTex) {   // caller caches these + forgets on device-lost
    if (!mkPlayer) mkPlayer = load_raw_texture_mip(dev, MK_PLAYER_PATH(), 64, 64);
    if (!mkMob)    mkMob    = load_raw_texture_mip(dev, MK_MOB_PATH(),    64, 64);
    if (!elemTex)  elemTex  = load_raw_texture_mip(dev, ELEM_ATLAS_PATH(), 256, 32);
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
    if (round) {
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
        dSetTex(dev, 0, 0); color_state(dev);
    } else if (round) {                                               // no map -> a plain lens hint
        disc_glow(dev, cx, cy, rc * 0.62f, 0x304A569Cu, rc * 0.5f);
    }

    // ---- markers + player pin : same transform / colours as the widget ; round -> circle clip, square -> rect clip ----
    if (g && g->map.valid) {
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
    if (mkPlayer) {                                                                // player Location pin at the centre (navy + halo)
        tex_state(dev, mkPlayer);
        const float pSz = snap(14.0f), pang = (g ? g->meHeading : 0.0f) + 1.5708f;
        tquad_rot(dev, snap(cx), snap(cy), pSz * 1.2f, pSz * 1.2f, pang, 0xF0FFFFFFu);
        tquad_rot(dev, snap(cx), snap(cy), pSz, pSz, pang, 0xFF12224Au);
        dSetTex(dev, 0, 0);
    }
    color_state(dev);
}

// the marker legend : one row each = the real marker (dot / mob Arrow / player pin, in its model colour) + its
// meaning. Clips per row against [top,bot] (like the leader-dots sample) so a long list scrolls cleanly.
float minimap_help_legend(u32 dev, Font* fo, u32 mkPlayer, u32 mkMob, float x, float y, float rowH, float top, float bot, int lang) {
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
    if (moonTex == 0 || moonKey != -2) {                             // bake the phase STRIP once (moonKey == -2 = baked)
        if (moonTex) { release_texture(moonTex); moonTex = 0; }
        static u32 strip[64 * (MOON_F * 64)];                        // 64 tall x (25*64) wide, baked once
        static u32 tmp[64 * 64];
        for (int fi = 0; fi < MOON_F; ++fi) {
            build_moon_argb(tmp, 64, 0xFFFFFFFFu, (float)fi / (float)(MOON_F - 1), false);   // WHITE, waxing -> tint/mirror at draw
            for (int yy = 0; yy < 64; ++yy) for (int xx = 0; xx < 64; ++xx) strip[yy * (MOON_F * 64) + fi * 64 + xx] = tmp[yy * 64 + xx];
        }
        moonTex = make_texture_argb_mip(dev, MOON_F * 64, 64, strip);
        moonKey = -2;
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
