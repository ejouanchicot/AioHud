// player.cpp -- see player.h. Phase 1 composition : box chrome + identity row (job emblem + name +
// main/sub job & levels) + 3 HP/MP/TP fioles (borrowed from the embedded LiquidBars) + a player buff tray.
#include "player.h"
#include "model/paths.h"
#include "liquid_bars.h"
#include "edit_box.h"           // shared edit-mode drag + alignment grid (Target/Player/future single boxes)
#include "model/gamestate.h"
#include "model/party_state.h"   // job_abbr / job_role_color
#include "model/ui_config.h"
#include "gfx/draw.h"
#include "gfx/texture.h"
#include "gfx/window.h"
#include "gfx/font.h"
#include "ui/text_style.h"       // te_sz/te_ow/te_col : shared TextStyle-resolve impl
#include "ui/ui_colors.h"        // mul_a : shared ARGB helper
#include "ui/box_style.h"        // draw_themed_box : shared box chrome (detached equipment frame)
#include <windows.h>
#include <string.h>
#include <stdio.h>

#include "ui/buff_atlas.h"

namespace aio {

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// mul_a -> ui/ui_colors.h (shared with target)

// the shared colour-quad render state (mirrors party/target setup_color_state) : untextured diffuse quads.
static void color_state(u32 dev) {
    dColorQuadState(dev);   // shared 2D colour-quad pipeline (gfx/d3d.h)
}

// job-emblem atlas (white masks, tinted per role) : 8 cols x 3 rows of 64px cells, in JOBS[1..22] order
// (WAR = cell 0 ... RUN = cell 21). Same asset the party badge uses (assets/job_icons.raw).
static const char* JOBICON_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\job_icons.raw"); return b; }
static const int JI_W = 512, JI_H = 192, JI_CELL = 64, JI_COLS = 8;

// gil coin icon (assets/icon_gil.raw) : 64x64 straight-alpha BGRA, drawn in the gil/speed band.
static const char* GIL_ICON_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\icon_gil.raw"); return b; }
static const int GIL_ICON_W = 64, GIL_ICON_H = 64;

// gear icons (equipment viewer) : bundled 32x32 BMPs named by item id, loaded on demand + cached per slot.
static const char* GEARICON_DIR() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\gearicons\\"); return b; }

// thousands-separated gil (e.g. 1234567 -> "1,234,567"), into `out` (>= 16 bytes for a u32).
static void format_gil(char* out, unsigned v) {
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);   // digits, reversed
    int o = 0;
    for (int i = n - 1; i >= 0; --i) { out[o++] = tmp[i]; if (i > 0 && i % 3 == 0) out[o++] = ','; }
    out[o] = 0;
}

// status-icon atlas (assets/buff_atlas.raw) : fixed 32-col grid of 32px cells ; id -> cell (id%32, id/32).

// textured-quad render state for an atlas draw (own copy : the party/target ones are file-static).
static void tex_state(u32 dev, u32 tex) { dTexQuadState(dev, tex); }   // mip NONE + CLAMP : the defaults
// one job emblem (white mask) from the atlas cell, tinted by the role colour. cell = job id - 1.
static void draw_job_emblem(u32 dev, u32 tex, float x, float y, float sz, int cell, u32 tint) {
    if (!tex || cell < 0) return;
    tex_state(dev, tex);
    const float au = (float)JI_CELL / (float)JI_W, av = (float)JI_CELL / (float)JI_H;
    const float cr = 6.0f / (float)JI_CELL;                       // crop the baked transparent margin
    const float cu = au * cr, cv = av * cr;
    const float u0 = (float)(cell % JI_COLS) * au + cu, u1 = (float)(cell % JI_COLS) * au + au - cu;
    const float v0 = (float)(cell / JI_COLS) * av + cv, v1 = (float)(cell / JI_COLS) * av + av - cv;
    tquad(dev, x, y, sz, sz, u0, u1, v0, v1, tint, tint);
    dSetTex(dev, 0, 0);
}

// per-element typography (ui_config().plrText[PLR_*]) : resolve Font face/weight/italic + size / outline /
// colour / UPPERCASE, on top of the widget's base size. Mirrors target.cpp's tgt_font family.
static Font* plr_font(FontManager* fm, Font* deflt, int elem) {
    if (!fm) return deflt;
    const TextStyle& t = ui_config().plrText[elem];
    const char* face = (t.face > 0) ? ui_font_face(t.face) : 0;   // 0 => the manager's default face
    Font* r = fm->get(face, t.bold ? 700 : 400, t.italic);
    return r ? r : deflt;
}
static inline float plr_sz(int e, float base) { return te_sz(ui_config().plrText[e], base); }
static inline float plr_ow(int e, float base) { return te_ow(ui_config().plrText[e], base); }
static inline u32   plr_col(int e, u32 base)  { return te_col(ui_config().plrText[e], base); }
static const char*  plr_up(int e, const char* s, char* buf, int cap) {   // UPPERCASE into buf if the element wants CAPS
    if (!ui_config().plrText[e].upper || !s) return s;
    int i = 0; for (; s[i] && i < cap - 1; ++i) { char c = s[i]; buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } buf[i] = 0; return buf;
}

static EditBox g_plrEdit;   // shared edit-mode drag state for the (single) Player box
static EditBox g_equipEdit; // edit-mode drag state for the STANDALONE equipment module (when detached from the Hub)

Player::Player(const GameState* state) : state_(state) {
    px_ = 304.0f; py_ = 636.0f;             // matches the layout default (place_widgets overrides it)
    vials_ = new LiquidBars(state);
}
Player::~Player() { delete vials_; }

void Player::measure(float& w, float& h) const { w = snap(324.0f * scale_); h = snap(150.0f * scale_); }


// Config-preview footprint : mirrors draw()'s size block (demo = a full 32-buff strip capped by plrBuffMax)
// so the preview positions the box knowing its REAL height (incl. an in-box equipment grid). Also reports
// how far a DOCKED grid reaches beyond the box on each side (l/t/r/b) so the preview keeps it in view.
void Player::preview_footprint(float& w, float& h, float& l, float& t, float& r, float& b) const {
    const UiConfig& c = ui_config();
    const float S = scale_ * clampf(c.plrScale, 0.5f, 2.0f);
    const bool showId = c.plrEmblem || c.plrName || c.plrLvl;
    int nbuff = 32, bmax = c.plrBuffMax; if (bmax < 1) bmax = 1; if (bmax > 32) bmax = 32; if (nbuff > bmax) nbuff = bmax;
    const bool showBuffs = c.plrBuffs && nbuff > 0;
    const int  nv = (c.plrHp ? 1 : 0) + (c.plrMp ? 1 : 0) + (c.plrTp ? 1 : 0);
    const float pad = snap(12.0f * S), innerW = snap(300.0f * S);
    const float outerPad = c.plrBox ? pad : 0.0f;   // EDGE margin only -> 0 with no box chrome (bounds hug content ; box ON byte-identical ; bar width innerW unchanged)
    const float emblemSz = c.plrEmblem ? snap(30.0f * S * clampf(c.plrEmblemSz, 0.5f, 2.0f)) : 0.0f;
    const float nameSz = c.plrName ? snap(plr_sz(PLR_NAME, 18.0f * S)) : 0.0f;
    const float lvlSz  = c.plrLvl ? snap(plr_sz(PLR_LVL, 12.0f * S)) : 0.0f;
    const float txtGap = (c.plrName && c.plrLvl) ? snap(2.0f * S) : 0.0f;
    const float textH = nameSz + lvlSz + txtGap;
    // the identity row hosts speed (left) + header-gil (right) : height = max(identity, their icon) so it's the
    // SAME with or without name/level (matches draw()).
    const float bandIcon = snap(18.0f * S);
    const float sgRowH = (c.plrSpeed || (c.plrGil && !c.plrEquip)) ? bandIcon : 0.0f;
    const float idBase = showId ? (emblemSz > textH ? emblemSz : textH) : 0.0f;
    const float idH = idBase > sgRowH ? idBase : sgRowH;
    const float sectGap = snap(10.0f * S);
    const float barH = snap(20.0f * S * clampf(c.plrBarH, 0.5f, 2.0f));
    const float barGap = snap(7.0f * S * clampf(c.plrBarGap, 0.0f, 3.0f));
    const float vH = nv > 0 ? nv * barH + (nv - 1) * barGap : 0.0f;
    const float bandH = 0.0f;                                  // speed + gil moved onto the identity row -> no separate band row
    const float bandGap = 0.0f;
    const float bIcon = snap(22.0f * S * clampf(c.plrIconSz, 0.5f, 2.0f)), bGap = snap(2.0f * S);
    int perRow = (int)((innerW + bGap) / (bIcon + bGap)); if (perRow < 1) perRow = 1;
    const int brows = showBuffs ? (nbuff + perRow - 1) / perRow : 0;
    const float bH = brows > 0 ? brows * bIcon + (brows - 1) * bGap : 0.0f;
    const float eqCell = snap(28.0f * S * clampf(c.plrEqCell, 0.5f, 2.0f)), eqGW = 4.0f * eqCell, colGap = snap(6.0f * S);
    const int   eqp = (c.plrEquip && !c.plrEquipDetach) ? c.plrEqPlace : -1;   // detached equipment reserves NO Hub space
    const bool  eqBelow = eqp >= 0 && eqp <= 2, eqSide = eqp == 7 || eqp == 8;
    const float gilBelowH = (c.plrEquip && c.plrGil) ? snap(24.0f * S) : 0.0f;   // gil row under the equip grid
    const float eqGrid = eqBelow ? (eqGW + gilBelowH) : 0.0f, sideW = eqSide ? (eqGW + colGap) : 0.0f;
    const float actNameSz = c.plrCast ? snap(plr_sz(PLR_NAME, 11.0f * S)) : 0.0f;   // action bar : ALWAYS reserved when on -> stable height
    const float actH = c.plrCast ? snap(actNameSz + snap(3.0f * S) + snap(6.0f * S) + snap(6.0f * S)) : 0.0f;
    const float vOff = outerPad + idH + (idH > 0.0f && nv > 0 ? sectGap : 0.0f);
    const float actOff = vOff + vH + (actH > 0.0f && (vH > 0.0f || idH > 0.0f) ? sectGap : 0.0f);
    const float bandOff = actOff + actH + (bandH > 0.0f && (actH > 0.0f || vH > 0.0f || idH > 0.0f) ? bandGap : 0.0f);
    const float bOff = bandOff + bandH + ((bandH > 0.0f || actH > 0.0f || vH > 0.0f) && brows > 0 ? sectGap : 0.0f);
    const float eqOff = bOff + bH + (eqGrid > 0.0f && (bH > 0.0f || bandH > 0.0f || actH > 0.0f || vH > 0.0f || idH > 0.0f) ? sectGap : 0.0f);
    w = innerW + 2.0f * outerPad + sideW;
    h = eqOff + eqGrid + outerPad;
    if (eqSide) { const float need = vOff + eqGW + gilBelowH + outerPad; if (h < need) h = need; }
    l = t = r = b = 0.0f;
    if (eqp >= 3 && eqp <= 6) {                                  // only the OUTSIDE docks reach beyond the box
        const float ext = eqGW + colGap;
        switch (eqp) { case 3: l = ext; break; case 4: r = ext; break; case 5: t = ext; break; case 6: b = ext; break; }
    }
}

// Config-preview footprint of the STANDALONE (detached) equipment box : the 4x4 grid plus a bounding reserve
// for the gil (below/above -> extra height ; left/right -> extra width), at the equipment's own live scale.
void Player::equip_footprint(float& w, float& h) const {
    const UiConfig& c = ui_config();
    const float S = scale_ * clampf(c.plrEquipScale, 0.5f, 2.0f);
    const float gridW = 4.0f * snap(28.0f * S * clampf(c.plrEqCell, 0.5f, 2.0f));
    w = gridW; h = gridW;
    if (c.plrGil) {
        const float gilB = snap(24.0f * S), gilRW = snap(80.0f * S);
        switch (c.plrEqGilPlace) { case 2: case 3: w += gilRW; break; default: h += gilB; break; }   // 1 above / 0 below both add height
    }
}

void Player::ensure(u32 dev) {
    if (!valid_ptr(dev)) return;
    vials_->ensure(dev);
    if (!jobicon_tex_ && !jobicon_tried_) { jobicon_tex_ = load_raw_texture(dev, JOBICON_PATH(), JI_W, JI_H); jobicon_tried_ = true; }
    if (!buff_tex_    && !buff_tried_)    { buff_tex_    = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H); buff_tried_ = true; }
    if (!gil_tex_     && !gil_tried_)     { gil_tex_     = load_raw_texture(dev, GIL_ICON_PATH(), GIL_ICON_W, GIL_ICON_H); gil_tried_ = true; }
}

void Player::on_device_lost() {   // FORGET handles (dead device) -> reload next ensure. Do NOT Release.
    vials_->on_device_lost();
    jobicon_tex_ = 0; jobicon_tried_ = false;
    buff_tex_ = 0; buff_tried_ = false;
    gil_tex_ = 0; gil_tried_ = false;
    for (int s = 0; s < 16; ++s) { gearTex_[s] = 0; gearId_[s] = 0; }   // FORGET handles + force reload
    plrSkin_.on_device_lost(); plrSkinVar_ = -1;
}

void Player::dispose() {
    vials_->dispose();
    release_texture(jobicon_tex_); jobicon_tex_ = 0; jobicon_tried_ = false;
    release_texture(buff_tex_);    buff_tex_ = 0;    buff_tried_ = false;
    release_texture(gil_tex_);     gil_tex_ = 0;     gil_tried_ = false;
    for (int s = 0; s < 16; ++s) { release_texture(gearTex_[s]); gearTex_[s] = 0; gearId_[s] = 0; }
    plrSkin_.dispose(); plrSkinVar_ = -1;
}

void Player::draw(const Frame& f) {
    if (!visible_ || !f.game) return;
    u32 dev = f.dev; const float t = f.t;
    const GameState& g = *f.game;
    const UiConfig& c = ui_config();
    const bool fake = demo_ || (c.editLayout && !g.inGame);
    if (!g.inGame && !fake) return;                              // out of game & not previewing -> nothing
    const float S = scale_ * clampf(c.plrScale, 0.5f, 2.0f);

    // ---- resolve the data (demo = a plausible PLD/WAR for the config preview) ----
    static const unsigned short DEMOB[32] = {   // a full 32-icon strip so the preview shows the Max Buffs layout
        40, 41, 42, 43, 44, 45, 33, 34, 35, 36, 37, 38, 39, 251, 116, 117,
        118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133 };
    const char* name = fake ? "PlayerName1" : g.me.name;
    const int   mjob = fake ? 7  : g.me.mjob;                    // 7 = PLD
    const int   sjob = fake ? 1  : g.me.sjob;                    // 1 = WAR
    const int   mlvl = fake ? 99 : g.me.mlvl;
    const int   slvl = fake ? 54 : g.me.slvl;
    const float hp = fake ? 1.00f : g.hp;
    const float mp = fake ? 0.42f : g.mp;
    const float tp = fake ? 0.61f : g.tp;
    const float meSpeed = fake ? 5.9f : g.meSpeed;              // movement_speed field ; 5.9 -> +18% demo sample
    const unsigned short* buffs = fake ? DEMOB : g.buffs;
    int nbuff = fake ? 32 : g.nbuff;   // demo : a full strip so Max Buffs is visible in the preview (capped below)
    int bmax = c.plrBuffMax; if (bmax < 1) bmax = 1; if (bmax > 32) bmax = 32;
    if (nbuff > bmax) nbuff = bmax; if (nbuff < 0) nbuff = 0;

    // ---- which sections are on (emblem / name / level each independent) ----
    const bool showEmblem = c.plrEmblem != 0;
    const bool showName   = c.plrName != 0;
    const bool showLvl    = c.plrLvl != 0;
    const bool showId     = showEmblem || showName || showLvl;
    const bool showBuffs  = c.plrBuffs && nbuff > 0;
    const bool showSpeed  = c.plrSpeed && (fake || meSpeed > 0.5f);   // hide until the self entity read succeeds (base 5.0 = valid)
    const bool showGil    = c.plrGil != 0;                            // gil is always readable in game (0 is valid) ; demo shows a sample
    const bool showEquip  = c.plrEquip != 0;                          // equipment viewer (4x4 grid of the 16 equipped items)
    int vkind[3]; int nv = 0;                                    // enabled vitals, in HP/MP/TP order
    if (c.plrHp) vkind[nv++] = 0;
    if (c.plrMp) vkind[nv++] = 1;
    if (c.plrTp) vkind[nv++] = 2;
    const float vfill[3] = { hp, mp, tp };

    // ---- sizes (independent of the box POSITION) ----
    const float pad    = snap(12.0f * S);
    const float outerPad = c.plrBox ? pad : 0.0f;   // EDGE margin only -> 0 with no box chrome (box ON byte-identical ; bar width innerW unchanged)
    const float innerW = snap(300.0f * S);
    const float emblemSz = showEmblem ? snap(30.0f * S * clampf(c.plrEmblemSz, 0.5f, 2.0f)) : 0.0f;
    const float nameSz   = showName   ? snap(plr_sz(PLR_NAME, 18.0f * S)) : 0.0f;
    const float lvlSz    = showLvl    ? snap(plr_sz(PLR_LVL, 12.0f * S)) : 0.0f;
    const float txtGap   = (showName && showLvl) ? snap(2.0f * S) : 0.0f;
    const float textH    = nameSz + lvlSz + txtGap;
    // idH : the identity row also HOSTS speed (left) + header-gil (right). With no emblem/name/level, still keep a
    // row for them so Speed isn't drawn into a zero-height row (its own line when Name/Level are hidden).
    // idH : the identity row also HOSTS speed (left) + header-gil (right). Height = max(identity, speed/gil icon)
    // so the row is the SAME height with or without name/level (speed alone no longer gets a taller row).
    const float sgRowH   = (showSpeed || (showGil && !showEquip)) ? snap(18.0f * S) : 0.0f;   // bandIcon (declared below) = 18*S
    const float idBase   = showId ? (emblemSz > textH ? emblemSz : textH) : 0.0f;
    const float idH      = idBase > sgRowH ? idBase : sgRowH;
    const float sectGap  = snap(10.0f * S);
    const float barH   = snap(20.0f * S * clampf(c.plrBarH, 0.5f, 2.0f));
    const float barW   = snap(innerW * clampf(c.plrBarW, 0.4f, 1.0f));
    const float barGap = snap(7.0f * S * clampf(c.plrBarGap, 0.0f, 3.0f));
    const float vH   = nv > 0 ? nv * barH + (nv - 1) * barGap : 0.0f;
    const float bandIcon = snap(18.0f * S);                      // gil/speed band : icon + label on one row
    const bool  bandShown = showGil || showSpeed;               // now drawn ON the identity row (speed left / gil right)
    const float bandH   = 0.0f;                                 // no separate band row -> speed + gil live on the name row
    const float bandGap = 0.0f;
    (void)bandShown;
    const float bIcon = snap(22.0f * S * clampf(c.plrIconSz, 0.5f, 2.0f)), bGap = snap(2.0f * S);
    int perRow = (int)((innerW + bGap) / (bIcon + bGap)); if (perRow < 1) perRow = 1;
    // RESERVE the buff rows for the whole Max-Buffs cap so the box height is STABLE whether buffs are up or not
    // (drawing below still uses the live nbuff via showBuffs). Matches measure()'s full-strip reservation.
    const int  brows = c.plrBuffs ? (bmax + perRow - 1) / perRow : 0;
    const float bH   = brows > 0 ? brows * bIcon + (brows - 1) * bGap : 0.0f;
    const float eqCell = snap(28.0f * S * clampf(c.plrEqCell, 0.5f, 2.0f));   // equipment viewer : 4x4 square of cells
    const float eqGW   = 4.0f * eqCell;                         // grid side length
    const bool  detachEq = showEquip && c.plrEquipDetach != 0;  // equipment is a STANDALONE module -> the Hub reserves no space for it
    const int   eqp    = (showEquip && !detachEq) ? c.plrEqPlace : -1;
    const bool  eqBelow = eqp >= 0 && eqp <= 2;                 // in-box, stacked BELOW the content (grows H)
    const bool  eqSide  = eqp == 7 || eqp == 8;                 // in-box, a COLUMN beside the content (widens W)
    const float colGap  = snap(6.0f * S);
    const float gilBelowH = (showEquip && c.plrGil) ? snap(24.0f * S) : 0.0f;   // gil row reserved centred UNDER the equip grid
    const float eqGrid  = eqBelow ? (eqGW + gilBelowH) : 0.0f;  // the below grid (+ its gil row) grows the box height
    const float sideW   = eqSide ? (eqGW + colGap) : 0.0f;      // a side column widens the box
    // ---- ACTION bar : the player's OWN live action (Magic cast / job ability / weaponskill) from the shared cast
    //      tracker (own 0x028 echoes back). Slot ALWAYS reserved (stable height) ; name + fill draw while acting. ----
    float castPct = 0.0f, castA = 0.0f; int castKind = 0; const char* castName = 0;
    if (fake) { castName = "Cure IV"; castPct = 0.55f; castA = 1.0f; castKind = 0; }   // preview sample
    else castName = party().cast_label(party().selfId_, castPct, castA, &castKind);
    const bool  actReserve = c.plrCast != 0;
    const bool  actShow    = actReserve && castName && castA > 0.01f;
    // the empty grey slot ("placeholder") : the reserved track drawn while idle, so you can position the bar.
    // Shown when actually casting OR when Cast-placeholder is on ; hidden (no bar) when idle + placeholder off.
    const bool  actTrack   = actReserve && (actShow || c.plrCastDemo != 0 || fake);
    const float actNameSz  = snap(plr_sz(PLR_CAST, 11.0f * S));
    const float actBarH    = snap(6.0f * S);
    const float actH       = actReserve ? snap(actNameSz + snap(3.0f * S) + actBarH + snap(6.0f * S)) : 0.0f;
    // vertical offsets from the box top -> the box height (position cancels out)
    const float idOff   = outerPad;
    const float vOff    = idOff + idH + (idH > 0.0f && nv > 0 ? sectGap : 0.0f);
    const float actOff  = vOff + vH + (actH > 0.0f && (vH > 0.0f || idH > 0.0f) ? sectGap : 0.0f);
    const float bandOff = actOff + actH + (bandH > 0.0f && (actH > 0.0f || vH > 0.0f || idH > 0.0f) ? bandGap : 0.0f);
    const float bOff    = bandOff + bandH + ((bandH > 0.0f || actH > 0.0f || vH > 0.0f) && brows > 0 ? sectGap : 0.0f);
    const float eqOff   = bOff + bH + (eqGrid > 0.0f && (bH > 0.0f || bandH > 0.0f || actH > 0.0f || vH > 0.0f || idH > 0.0f) ? sectGap : 0.0f);
    const float W = innerW + 2.0f * outerPad + sideW;
    float H = eqOff + eqGrid + outerPad;
    if (eqSide) { const float need = vOff + eqGW + gilBelowH + outerPad; if (H < need) H = need; }   // header + side grid + its gil row below

    // ---- position : user-placed fraction wins over the layout default ; centre-lock re-centres each frame
    //      (survives resolution changes ; a drag releases it). The config preview uses set_origin (demo_). ----
    float px, py;
    if (!demo_ && c.plrPosSet && f.screenW > 0.0f && f.screenH > 0.0f) {
        px = snap(c.plrX * f.screenW); py = snap(c.plrY * f.screenH);
    } else { px = snap(px_); py = snap(py_); }
    if (!demo_) {
        if (c.plrCenterH && f.screenW > 0.0f) px = snap((f.screenW - W) * 0.5f);
        if (c.plrCenterV && f.screenH > 0.0f) py = snap((f.screenH - H) * 0.5f);
    }

    // ---- EDIT MODE : shared drag (move + Shift/Ctrl axis-lock + centre snap + zone push-out + wheel resize)
    //      + the alignment grid, identical to the Target box (one implementation in edit_box.cpp). A DOCKED
    //      equipment grid extends the CLICK area (union of the box + the grid) so grabbing the grid drags all. ----
    float hx = px, hy = py, hw = W, hh = H;
    if (showEquip && !detachEq && c.plrEqPlace >= 3 && c.plrEqPlace <= 6) {   // only the OUTSIDE docks extend the click area
        const float gw = 4.0f * eqCell, dg = snap(6.0f * S);
        float gX, gY;
        switch (c.plrEqPlace) {
            case 3:  gX = px - dg - gw;          gY = py + (H - gw) * 0.5f; break;   // dock left
            case 4:  gX = px + W + dg;           gY = py + (H - gw) * 0.5f; break;   // dock right
            case 5:  gX = px + (W - gw) * 0.5f;  gY = py - dg - gw; break;           // dock top
            default: gX = px + (W - gw) * 0.5f;  gY = py + H + dg; break;            // dock bottom
        }
        const float l = px < gX ? px : gX, tp = py < gY ? py : gY;
        const float rr = (px + W) > (gX + gw) ? (px + W) : (gX + gw);
        const float bb = (py + H) > (gY + gw) ? (py + H) : (gY + gw);
        hx = l; hy = tp; hw = rr - l; hh = bb - tp;
    }
    if (c.plrShow && c.editLayout && !demo_ &&
        edit_box_drag(g_plrEdit, EDITBOX_PLAYER, f, px, py, W, H, ZPERM_HUB,
                      ui_config().plrPosSet, ui_config().plrX, ui_config().plrY,
                      ui_config().plrCenterH, ui_config().plrCenterV, ui_config().plrScale, hx, hy, hw, hh))
        edit_box_grid(dev, f, g_plrEdit, px, py, W, H, ui_config().plrCenterH != 0, ui_config().plrCenterV != 0);

    // ---- draw positions (relative to the resolved box origin) ----
    const float x = px, y = py;
    const float ix   = x + outerPad + (eqp == 7 ? (eqGW + colGap) : 0.0f);   // content column shifts right for a LEFT side grid
    const float idY  = y + idOff;
    const float vTop = y + vOff;
    const float bTop = y + bOff;
    const float eqTop = y + eqOff;
    const float barX = ix + (innerW - barW) * 0.5f;              // centred

    // The whole Hub (chrome + identity + vitals + buffs + cast) draws only when the Player module is ON. When
    // it's OFF we fall straight through to the equipment block below, which STILL renders if equipment is a
    // STANDALONE module -> the gear grid can be kept even with the Hub hidden. (Body kept at its original indent
    // to keep this a minimal, reviewable diff.)
    if (c.plrShow) {
    // ---- box chrome : own Box Theme (Copy Party / procedural family / FFXI-texture), like the Target box ----
    if (c.plrBox) {
        color_state(dev);
        const float ca = clampf(c.plrBoxAlpha, 0.0f, 1.0f);   // full 0..100% (0 = box fully invisible)
        const u32   tint = mul_a(0xFFFFFFFF, ca);
        const bool  copyParty = c.plrThemeCopy != 0;
        const int   theme = copyParty ? c.skinTheme : c.plrTheme;
        const float lum   = copyParty ? c.skinLum   : c.plrLum;
        const unsigned hue = copyParty ? c.skinHue  : c.plrHue;   // custom box hue (0 = preset)
        if (window_theme_is_proc(theme)) {
            draw_proc_window(dev, theme, x, y, W, H, tint, false, true, lum, hue);
        } else if (copyParty && f.skin && f.skin->ready()) {
            draw_window(dev, *f.skin, x, y, W, H, tint, S, false, true);   // reuse the party's already-loaded FFXI skin
        } else {
            if (plrSkinVar_ != theme) { plrSkin_.dispose(); plrSkin_.load(dev, window_theme_name(theme)); plrSkinVar_ = theme; }
            if (plrSkin_.ready()) draw_window(dev, plrSkin_, x, y, W, H, tint, S, false, true);
            else rrect_bordered(dev, x, y, W, H, snap(6.0f * S), mul_a(0xFF232E54, ca), mul_a(0xFF080B1A, ca), mul_a(0x6699BBFF, ca), 1.0f);
        }
    }

    // ---- identity ROW : [Speed %] far-left · [emblem][name / jobs] centre · [Gil] far-right. Speed & gil used to
    //      be a separate band below ; merged here to save a row (speed pinned left, gil pinned right). ----
    const u32 role = (job_role_color(mjob) & 0x00FFFFFF) | 0xFF000000u;
    const float idCy = idY + idH * 0.5f, tgapId = snap(4.0f * S);
    const float hL = x + outerPad, hR = x + W - outerPad;                  // header spans the WHOLE box width (over the side equip column, which starts below)
    char up[48];
    float idLeft = hL, idRight = hR;
    if (showSpeed && buff_tex_) {                                // SPEED : buff-atlas cell 32 + "+NN%", pinned LEFT
        float pct = 100.0f * (meSpeed / 5.0f - 1.0f); if (pct > 60.0f) pct = 60.0f; if (pct < -60.0f) pct = -60.0f;
        const int ip = (int)(pct + (pct >= 0.0f ? 0.5f : -0.5f));
        const u32 scol = ip > 1 ? 0xFF78E678u : (ip < -1 ? 0xFFFF4646u : 0xFFC8C8C8u);
        char sb[16]; sprintf(sb, "%+d%%", ip);
        Font* pf = plr_font(f.fonts, f.font, PLR_SPEED);
        const float psz = snap(plr_sz(PLR_SPEED, 15.0f * S));
        const char* ps = plr_up(PLR_SPEED, sb, up, 48);
        const float tw = pf ? pf->measure(ps, psz) : 0.0f;
        const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
        const float u0 = (float)(32 % BUFF_COLS) * au, v0 = (float)(32 / BUFF_COLS) * av;
        tex_state(dev, buff_tex_);
        tquad(dev, hL, snap(idCy - bandIcon * 0.5f), bandIcon, bandIcon, u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
        dSetTex(dev, 0, 0);
        if (pf) { pf->begin(dev); pf->draw_lc(dev, hL + bandIcon + tgapId, idCy, ps, psz, plr_col(PLR_SPEED, scol), 0xFF000000u, plr_ow(PLR_SPEED, 1.4f * S)); }
        idLeft = hL + bandIcon + tgapId + tw + snap(10.0f * S);
    }
    if (showGil && gil_tex_ && !showEquip) {                     // GIL on the header (RIGHT) only when there's NO equipment
        char gb[16]; format_gil(gb, fake ? 1234567u : g.meGil);  // viewer ; with the equipment shown it sits centred BELOW the grid (see the equip block)
        Font* gf = plr_font(f.fonts, f.font, PLR_GIL);
        const float gsz = snap(plr_sz(PLR_GIL, 15.0f * S));
        const char* gs = plr_up(PLR_GIL, gb, up, 48);
        const float tw = gf ? gf->measure(gs, gsz) : 0.0f;
        const float gx = snap(hR - (bandIcon + tgapId + tw));
        tex_state(dev, gil_tex_);
        tquad(dev, gx, snap(idCy - bandIcon * 0.5f), bandIcon, bandIcon, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
        dSetTex(dev, 0, 0);
        if (gf) { gf->begin(dev); gf->draw_lc(dev, gx + bandIcon + tgapId, idCy, gs, gsz, plr_col(PLR_GIL, 0xFFFFE48Cu), 0xFF000000u, plr_ow(PLR_GIL, 1.4f * S)); }
        idRight = gx - snap(10.0f * S);
    }
    // [emblem][name / jobs] CENTRED horizontally in the header (between the speed block on the left and the gil on
    // the right : measure the group, centre it on the box, then clamp so it never overlaps speed/gil).
    if (showEmblem || showName || showLvl) {
        Font* nf = plr_font(f.fonts, f.font, PLR_NAME);
        Font* lf = plr_font(f.fonts, f.font, PLR_LVL);
        char nbuf[48], lbuf[48], sub[48];
        const char* ns = (showName && name && name[0]) ? plr_up(PLR_NAME, name, nbuf, 48) : 0;
        const char* ls = 0;
        if (showLvl && mjob > 0) {
            if (mlvl > 0) sprintf(sub, "%s %d / %s %d", job_abbr(mjob), mlvl, job_abbr(sjob), slvl);
            else          sprintf(sub, "%s / %s", job_abbr(mjob), job_abbr(sjob));
            ls = plr_up(PLR_LVL, sub, lbuf, 48);
        }
        const float nw = (ns && nf) ? nf->measure(ns, nameSz) : 0.0f;
        const float lw = (ls && lf) ? lf->measure(ls, lvlSz) : 0.0f;
        const float textW = nw > lw ? nw : lw;
        const float embW = (showEmblem && jobicon_tex_ && mjob > 0) ? (emblemSz + snap(9.0f * S)) : 0.0f;
        const float blockW = embW + textW;
        float bl = snap((hL + hR) * 0.5f - blockW * 0.5f);        // centre on the box ...
        if (bl < idLeft) bl = idLeft;                             // ... but never under the speed block
        if (bl + blockW > idRight) bl = snap(idRight - blockW);   // ... or the gil
        if (embW > 0.0f) draw_job_emblem(dev, jobicon_tex_, bl, idY + (idH - emblemSz) * 0.5f, emblemSz, mjob - 1, role);
        const float tx = bl + embW;
        float ty = idY + (idH - textH) * 0.5f;                    // top of the (centred) text stack
        if (ns && nf) { nf->begin(dev); nf->draw_lc(dev, tx, ty + nameSz * 0.5f, ns, nameSz, plr_col(PLR_NAME, 0xFFF2F4F7u), 0xFF000000u, plr_ow(PLR_NAME, 1.4f * S)); ty += nameSz + txtGap; }
        if (ls && lf) { lf->begin(dev); lf->draw_lc(dev, tx, ty + lvlSz * 0.5f, ls, lvlSz, plr_col(PLR_LVL, 0xFFB8C0CCu), 0xFF000000u, plr_ow(PLR_LVL, 1.4f * S)); }
    }

    // ---- vitals : the enabled real fioles (borrowed assets), stacked ----
    if (nv > 0 && vials_->vial_ready()) {
        float by = vTop;
        for (int i = 0; i < nv; ++i) {
            const int k = vkind[i]; const float fl = vfill[k];
            const u32   col = k == 0 ? 0xFF40C040u : (k == 1 ? 0xFF4C8CF0u : 0xFFB070FFu);
            const float pulse  = k == 2 ? (fl >= 0.3333f ? 1.0f : 0.0f) : 0.0f;
            const float danger = k == 0 ? (fl <= 0.25f ? 1.0f : 0.0f) : 0.0f;
            vials_->draw_vial_scaled(dev, t, barX, by, barW, barH, k, fl, col, pulse, danger, 4);
            by += barH + barGap;
        }
        // HP / MP / TP value centred on each fiole, per-element typography (PLR_HP/MP/TP). Own font pass per vital
        // (each may use a different face) -- keeps the liquid render state intact.
        {
            const int vval[3] = { fake ? 1450 : g.me.hp, fake ? 620 : g.me.mp, fake ? 1850 : g.me.tp };
            float ny = vTop;
            for (int i = 0; i < nv; ++i) {
                const int k = vkind[i], te = PLR_HP + k;                 // 0=HP,1=MP,2=TP -> PLR_HP/PLR_MP/PLR_TP
                Font* vf = plr_font(f.fonts, f.font, te);
                if (vf) {
                    vf->begin(dev);
                    char vb[12]; sprintf(vb, "%d", vval[k]);
                    vf->draw_c(dev, snap(barX + barW * 0.5f), snap(ny + barH * 0.5f), vb, snap(plr_sz(te, barH * 0.56f)), plr_col(te, 0xFFF4FAFFu), 0xFF000000u, plr_ow(te, 1.8f * S));
                }
                ny += barH + barGap;
            }
        }
    }

    // ---- ACTION bar : the player's own live action -> Magic cast (purple) / job ability (cyan) / weaponskill
    //      (orange), name + filling bar. Slot ALWAYS reserved (faint track) so the box height never jumps. ----
    if (actTrack) {   // the reserved slot : empty grey track when idle+placeholder, filled during a cast. Height (actH)
                      // stays reserved on plrCast so the box never jumps ; the grey track is gone when idle + placeholder off.
        const float actTop = y + actOff;
        const float aby = snap(actTop + actNameSz + snap(3.0f * S)), abr = actBarH * 0.5f;
        color_state(dev);
        rrect(dev, barX, aby, barW, actBarH, abr, 0x50121828u, 0x500C1120u, snap(1.0f * S));   // faint reserved track
        if (actShow) {
            rrect(dev, barX, aby, barW, actBarH, abr, mul_a(0xC0121828u, castA), mul_a(0xC00C1120u, castA), snap(1.0f * S));   // solid track under the fill
            const u32 cTop = castKind == 0 ? 0xFF6FA8FFu : (castKind == 2 ? 0xFFFFD65Au : 0xFFF2B45Au);   // spell BLUE / JA YELLOW / WS-TP orange
            const u32 cBot = castKind == 0 ? 0xFF4C82E6u : (castKind == 2 ? 0xFFE0A828u : 0xFFE08A2Eu);
            const float cp = castPct < 0.0f ? 0.0f : (castPct > 1.0f ? 1.0f : castPct);
            const float fw = barW * cp;
            if (fw > 1.5f) rrect_left(dev, barX, aby, fw, actBarH, abr, mul_a(cTop, castA), mul_a(cBot, castA), snap(1.0f * S));
            Font* af = plr_font(f.fonts, f.font, PLR_CAST);
            if (af) { af->begin(dev); char ab[40]; const char* an = plr_up(PLR_CAST, castName, ab, 40);
                af->draw_lc(dev, ix, snap(actTop + actNameSz * 0.5f), an, actNameSz, mul_a(plr_col(PLR_CAST, 0xFFE9DEFFu), castA), 0xFF000000u, plr_ow(PLR_CAST, 1.2f * S)); }
        }
    }

    // (gil + speed are now drawn on the identity row above -- no separate band row.)

    // ---- buffs tray : status icons wrapped into rows under the vitals ----
    if (buff_tex_ && showBuffs) {
        tex_state(dev, buff_tex_);
        const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
        for (int i = 0; i < nbuff; ++i) {
            const int id = buffs[i]; if (id < 0 || id >= BUFF_COLS * BUFF_ATLAS_ROWS) continue;
            const int col = i % perRow, row = i / perRow;
            const float bx = snap(ix + col * (bIcon + bGap));
            const float byy = snap(bTop + row * (bIcon + bGap));
            const float u0 = (float)(id % BUFF_COLS) * au, v0 = (float)(id / BUFF_COLS) * av;
            tquad(dev, bx, byy, bIcon, bIcon, u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
        }
        dSetTex(dev, 0, 0);
    }
    }   // end if (c.plrShow) -- the whole Hub body ; the equipment block below still draws when STANDALONE

    // ---- equipment viewer : 4x4 grid of the 16 equipped items, each with its real gear icon (bundled 32x32
    //      BMP named by item id, loaded on demand + cached per slot). Slot -> grid position via EQ_DPOS
    //      (the addon's player_equip layout). Missing-icon slots fall back to the item-id text. ----
    if (showEquip && !(detachEq && demo_ && !pvEq_) && (c.plrShow || detachEq || pvEq_)) {   // draws when the Hub is shown (docked or standalone) OR whenever detached ; in the config PREVIEW only when pvEq_ forces it (Player page shows the detached grid as a group with the Hub)
        // DOCKED uses the Hub's scale ; STANDALONE uses its own (plrEquipScale) -> shadow S / eqCell for the whole block.
        const float S = detachEq ? (scale_ * clampf(c.plrEquipScale, 0.5f, 2.0f)) : (scale_ * clampf(c.plrScale, 0.5f, 2.0f));
        const float eqCell = snap(28.0f * S * clampf(c.plrEqCell, 0.5f, 2.0f));
        static const int EQ_DPOS[16] = { 0, 1, 2, 3, 4, 8, 9, 14, 15, 5, 13, 6, 7, 10, 11, 12 };
        // in-game (even in the config preview) show the player's REAL equipment ; only fabricate a demo
        // set when truly out of game (so the box still populates for out-of-game layout tuning).
        const bool eqDemo = fake && !g.inGame;
        EquipSet demoEq;
        if (eqDemo) for (int s = 0; s < 16; ++s) { demoEq.id[s] = (unsigned short)(10297 + s); demoEq.count[s] = (unsigned short)((s == 3) ? 99 : 1); }
        const EquipSet& eq = eqDemo ? demoEq : g.equip;
        // Only sync when the equip read actually RESOLVED. During a zone / not-logged-in the item containers
        // aren't ready -> read_equipment returns all-zero (equipValid=false) ; syncing then would release all 16
        // cached icons and force a reload storm exactly when the device is busiest (the "rebug apres chargement").
        // Skip -> the cached gearTex_ persist across the zone, mirroring how the addon keeps its icons on 0x0A/0x0B.
        const bool equipReady = eqDemo || g.equipValid;
        // sync per-slot textures : (re)load gearicons/<id>.bmp only when a slot's equipped item id changes.
        if (equipReady) for (int s = 0; s < 16; ++s) {
            const unsigned short want = (eq.id[s] != 0 && eq.id[s] != 0xFFFF) ? eq.id[s] : 0;
            // (Re)load when the slot's item changed OR the load hasn't succeeded yet (gearTex_ still 0 for a
            // non-empty slot whose BMP exists -> a transient create failure ; keep retrying until it sticks).
            const bool needLoad = (gearId_[s] != want) || (want != 0 && gearTex_[s] == 0 && gearTry_[s] != 255);
            if (needLoad) {
                if (gearId_[s] != want) { if (gearTex_[s]) { release_texture(gearTex_[s]); gearTex_[s] = 0; } gearTry_[s] = 0; }   // new item -> drop the old + reset retries
                if (!want) { gearId_[s] = want; }
                else {
                    char p[160]; sprintf(p, "%s%u.bmp", GEARICON_DIR(), want);
                    u32 tex = load_bmp_texture(dev, p);
                    if (!tex && GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES && decode_gear_icon_from_rom(want, p))
                        tex = load_bmp_texture(dev, p);   // not in the bundled seed -> decode it once from the game's ROM DAT (cached to gearicons/ for next time)
                    if (tex) { gearTex_[s] = tex; gearId_[s] = want; gearTry_[s] = 0; }        // loaded -> cache it
                    else if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) { gearId_[s] = want; gearTry_[s] = 255; }   // no icon anywhere (no game install / unknown id) -> id-text fallback, DON'T retry (255 = give up)
                    else { gearId_[s] = want; if (gearTry_[s] < 254) ++gearTry_[s]; }          // BMP EXISTS but create failed -> retry every frame until it succeeds (never gives up on a real file ; capped <255 so it can't hit the give-up sentinel)
                }
            }
        }
        const float gridW = 4.0f * eqCell, dgap = snap(6.0f * S);
        float gx0, gy0;
        if (detachEq) {
            // STANDALONE MODULE : own dragged position (a fraction of the screen) + own size. The gil row sits
            // just under the grid -> include it in the drag/hit height so grabbing anywhere on it moves all.
            const float gilB = (showGil && c.plrEqGilPlace == 0) ? snap(24.0f * S) : 0.0f;   // gil below -> taller hit rect
            const float gilR = (showGil && c.plrEqGilPlace == 3) ? snap(80.0f * S) : 0.0f;   // gil right -> wider hit rect
            const float ew = gridW + gilR, eh = gridW + gilB;   // above/left draw outside the -x/-y edge ; the grid stays the grab target
            if (pvEq_) {
                // CONFIG PREVIEW : pvEqX_/Y_ is the footprint top-left in the stage ; push the grid past an
                // above/left gil so the whole box stays inside the group (matches equip_footprint's reserve).
                float ox = 0.0f, oy = 0.0f;
                if (showGil) { if (c.plrEqGilPlace == 2) ox = snap(80.0f * S); else if (c.plrEqGilPlace == 1) oy = snap(24.0f * S); }
                gx0 = pvEqX_ + ox; gy0 = pvEqY_ + oy;
            } else if (!demo_ && c.plrEquipPosSet && f.screenW > 0.0f && f.screenH > 0.0f) {
                gx0 = c.plrEquipX * f.screenW; gy0 = c.plrEquipY * f.screenH;
            } else { gx0 = px + W + snap(12.0f * S); gy0 = py; }   // first time : just right of the Hub
            if (c.editLayout && !demo_) {
                int chNo = 0, cvNo = 0;   // no centre-lock for the equipment module
                if (edit_box_drag(g_equipEdit, EDITBOX_EQUIP, f, gx0, gy0, ew, eh, ZPERM_HUB,
                                  ui_config().plrEquipPosSet, ui_config().plrEquipX, ui_config().plrEquipY,
                                  chNo, cvNo, ui_config().plrEquipScale))
                    edit_box_grid(dev, f, g_equipEdit, gx0, gy0, ew, eh, false, false);
            }
        } else {
            // placement : 0 in-box centre, 1 in-box left, 2 in-box right, 3 dock left, 4 dock right, 5 dock top,
            //             6 dock bottom, 7 side column LEFT (in-box), 8 side column RIGHT (in-box).
            switch (c.plrEqPlace) {
                case 1:  gx0 = ix;                          gy0 = eqTop; break;                       // in-box below, left
                case 2:  gx0 = ix + innerW - gridW;         gy0 = eqTop; break;                       // in-box below, right
                case 3:  gx0 = x - dgap - gridW;            gy0 = y + (H - gridW) * 0.5f; break;       // dock left  (v-centred)
                case 4:  gx0 = x + W + dgap;                gy0 = y + (H - gridW) * 0.5f; break;       // dock right
                case 5:  gx0 = x + (W - gridW) * 0.5f;      gy0 = y - dgap - gridW; break;             // dock top   (h-centred)
                case 6:  gx0 = x + (W - gridW) * 0.5f;      gy0 = y + H + dgap; break;                 // dock bottom
                case 7:  gx0 = x + outerPad;                     gy0 = y + vOff; break;       // side column LEFT : starts BELOW the full-width header (content shifted right)
                case 8:  gx0 = x + outerPad + innerW + colGap;   gy0 = y + vOff; break;       // side column RIGHT : starts below the header too
                default: gx0 = ix + (innerW - gridW) * 0.5f; gy0 = eqTop; break;                       // in-box below, centre
            }
        }
        gx0 = snap(gx0); gy0 = snap(gy0);
        // STANDALONE : a shared themed box (bg frame + border) wraps the grid + gil area, like the other detached
        // modules. DOCKED shares the Hub's own box, so this only draws when detached. Rect = grid + the same gil
        // reserve the hit-rect/preview use (24*S above/below, 80*S left/right) + a small pad. Drawn BEFORE the cells ;
        // draw_themed_box restores its own D3D state and the cells re-issue color_state() below.
        if (detachEq && c.plrEqBox.on) {
            const float bpad = snap(8.0f * S);
            float bcx = gx0, bcy = gy0, bcw = gridW, bch = gridW;   // content rect = the 4x4 grid
            if (showGil) switch (c.plrEqGilPlace) {
                case 1:  { const float e = snap(24.0f * S); bcy -= e; bch += e; } break;   // gil above
                case 2:  { const float e = snap(80.0f * S); bcx -= e; bcw += e; } break;   // gil left
                case 3:  bcw += snap(80.0f * S); break;                                     // gil right
                default: bch += snap(24.0f * S); break;                                     // gil below
            }
            draw_themed_box(dev, f.skin, snap(bcx - bpad), snap(bcy - bpad), snap(bcw + 2.0f * bpad), snap(bch + 2.0f * bpad), c.plrEqBox, 1.0f, S);
        }
        const float cellSz = eqCell - snap(1.0f), r = snap(3.0f * S), ipad = snap(2.0f * S), isz = cellSz - 2.0f * ipad;
        // BORDER : per-cell borders only (NO outer frame). "Box Theme" makes each cell's border the theme's real
        // FRAME colour (Royal gold, Medieval iron, FFXI skin border, ...) ; "Custom" uses the chosen colour.
        const bool copyParty = c.plrThemeCopy != 0;
        const int  eqTheme = copyParty ? c.skinTheme : c.plrTheme;
        const unsigned eqHue = copyParty ? c.skinHue : c.plrHue;   // custom box hue (0 = preset) -> tint the cell borders too
        u32 accB;                                               // per-cell border RGB (alpha applied per state below)
        if (c.plrEqThemeBorder) {
            const bool proc = window_theme_is_proc(eqTheme);
            const WindowSkin* sk = proc ? (const WindowSkin*)0 : (copyParty ? f.skin : (plrSkin_.ready() ? &plrSkin_ : (const WindowSkin*)0));
            accB = (proc ? box_theme_border_color(eqTheme, eqHue) : (sk && sk->ready() ? sk->borderColor : 0xFF6699BBu)) & 0x00FFFFFFu;
        } else {
            accB = c.plrEqColor & 0x00FFFFFFu;                  // custom : per-cell coloured border
        }
        // cells (bg + border). Box-theme + procedural family -> each cell's border is the theme's REAL frame
        // (gold plate / iron-steel bevel / neon tube / frost rime), matching the current theme -- not a flat
        // colour. FFXI-theme / custom -> a flat coloured border (accB).
        const bool themedCells = c.plrEqThemeBorder && window_theme_is_proc(eqTheme);
        // cell BACKGROUND : custom colour (occupied = plrEqCellBg ; the gradient bottom + empty cells are derived
        // shades so filled slots still read brighter) or the default dark. Same colours for the themed + flat paths.
        const bool eqBgC = c.plrEqCellBgCustom != 0;
        const u32 bgOccT = eqBgC ? c.plrEqCellBg : 0xE0121620u, bgOccB = eqBgC ? scl(c.plrEqCellBg, 0.72f) : 0xE00A0F16u;
        const u32 bgEmpT = eqBgC ? scl(c.plrEqCellBg, 0.55f) : 0xC00E0E12u, bgEmpB = eqBgC ? scl(c.plrEqCellBg, 0.40f) : 0xC0060608u;
        color_state(dev);
        if (themedCells) {
            // FUSED themed grid : cell backgrounds abut EDGE-TO-EDGE, then ONE themed frame around the whole 4x4 +
            // single shared internal grid lines (theme colour) -- so the border between two cases is ONE line, not
            // two doubled per-cell frames.
            for (int s = 0; s < 16; ++s) {
                const int dp = EQ_DPOS[s]; const bool occ = eq.id[s] != 0 && eq.id[s] != 0xFFFF;
                const float cx = snap(gx0 + (dp % 4) * eqCell), cy = snap(gy0 + (dp / 4) * eqCell);
                const u32 bt = occ ? bgOccT : bgEmpT, bb = occ ? bgOccB : bgEmpB;
                grad_quad(dev, cx, cy, snap(eqCell), snap(eqCell), bt, bt, bb, bb);   // full-cell bg (no gap)
            }
            if (window_theme_family(eqTheme) == 4) {   // NEON : internal lattice as neon tubes (colour/white/colour),
                const float lt = snap(4.0f * S);       // then the SEAMLESS outer frame LAST, on top of the lattice.
                for (int i = 1; i < 4; ++i) {          // internal dividers only
                    draw_neon_line(dev, eqTheme, snap(gx0 + i * eqCell) - lt * 0.5f, snap(gy0), lt, snap(gridW), eqHue);   // vertical
                    draw_neon_line(dev, eqTheme, snap(gx0), snap(gy0 + i * eqCell) - lt * 0.5f, snap(gridW), lt, eqHue);   // horizontal
                }
                draw_neon_frame(dev, eqTheme, snap(gx0) - lt * 0.5f, snap(gy0) - lt * 0.5f, snap(gridW) + lt, snap(gridW) + lt, lt, eqHue);   // seamless outer tube, on top
            } else {
                const u32 gl = box_theme_border_color(eqTheme, eqHue); const float lt = snap(1.5f * S);
                for (int i = 1; i < 4; ++i) {   // internal dividers only (the outer edge is the themed frame below)
                    grad_quad(dev, snap(gx0 + i * eqCell) - lt * 0.5f, snap(gy0), lt, snap(gridW), gl, gl, gl, gl);   // vertical
                    grad_quad(dev, snap(gx0), snap(gy0 + i * eqCell) - lt * 0.5f, snap(gridW), lt, gl, gl, gl, gl);   // horizontal
                }
                draw_box_border_rect(dev, eqTheme, snap(gx0), snap(gy0), snap(gridW), snap(gridW), 0.0f, eqHue);   // one themed frame around the grid (custom hue like the dividers)
            }
            color_state(dev);
        } else {
            for (int s = 0; s < 16; ++s) {   // flat coloured separators between ALL cases (FFXI / custom)
                const int dp = EQ_DPOS[s]; const bool occ = eq.id[s] != 0 && eq.id[s] != 0xFFFF;
                rrect_bordered(dev, snap(gx0 + (dp % 4) * eqCell), snap(gy0 + (dp / 4) * eqCell), cellSz, cellSz, r,
                               occ ? bgOccT : bgEmpT, occ ? bgOccB : bgEmpB,
                               accB | (occ ? 0xCC000000u : 0x99000000u), 1.0f);
            }
        }
        // gear icons over occupied cells
        for (int s = 0; s < 16; ++s) {
            if (!gearTex_[s]) continue;
            const int dp = EQ_DPOS[s];
            const float cx = snap(gx0 + (dp % 4) * eqCell), cy = snap(gy0 + (dp / 4) * eqCell);
            tex_state(dev, gearTex_[s]);
            tquad(dev, snap(cx + ipad), snap(cy + ipad), isz, isz, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
            dSetTex(dev, 0, 0);
        }
        // encumbrance : a red X over each LOCKED equip slot (0x01B flags ; bit sid = slot sid is locked).
        const unsigned enc = fake ? 0u : party().encumbrance();
        if (enc) {
            color_state(dev);
            const float m = snap(5.0f * S), th = snap(2.5f * S);
            const u32 red = 0xD2FF3838u;
            for (int s = 0; s < 16; ++s) {
                if (!((enc >> s) & 1u)) continue;
                const int dp = EQ_DPOS[s];
                const float cx = snap(gx0 + (dp % 4) * eqCell), cy = snap(gy0 + (dp / 4) * eqCell);
                seg_soft(dev, cx + m, cy + m, cx + cellSz - m, cy + cellSz - m, th, red);
                seg_soft(dev, cx + cellSz - m, cy + m, cx + m, cy + cellSz - m, th, red);
            }
        }
        // GIL : coin icon + amount, drawn WITH the grid (the header carries it only when there is no equipment
        // viewer). Docked -> always centred below the grid. Standalone -> the chosen side (below/above/left/right).
        if (showGil && gil_tex_) {
            char gb[16]; format_gil(gb, fake ? 1234567u : g.meGil);
            Font* gf = plr_font(f.fonts, f.font, PLR_GIL);
            const float gsz = snap(plr_sz(PLR_GIL, 15.0f * S));
            char gu[24]; const char* gs = plr_up(PLR_GIL, gb, gu, 24);
            const float tw = gf ? gf->measure(gs, gsz) : 0.0f;
            const float blockW = bandIcon + snap(4.0f * S) + tw;
            const float ggap = snap(6.0f * S);
            const int   gpl = detachEq ? c.plrEqGilPlace : 0;   // docked always below
            float glx, gcy;                                     // glx = block left, gcy = block vertical centre
            switch (gpl) {
                case 1:  glx = snap(gx0 + gridW * 0.5f - blockW * 0.5f); gcy = snap(gy0 - ggap - bandIcon * 0.5f); break;   // above (h-centred)
                case 2:  glx = snap(gx0 - ggap - blockW);               gcy = snap(gy0 + gridW * 0.5f); break;             // left  (v-centred)
                case 3:  glx = snap(gx0 + gridW + ggap);                gcy = snap(gy0 + gridW * 0.5f); break;             // right (v-centred)
                default: glx = snap(gx0 + gridW * 0.5f - blockW * 0.5f); gcy = snap(gy0 + gridW + ggap + bandIcon * 0.5f); break;   // below (h-centred)
            }
            tex_state(dev, gil_tex_);
            tquad(dev, glx, snap(gcy - bandIcon * 0.5f), bandIcon, bandIcon, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
            dSetTex(dev, 0, 0);
            if (gf) { gf->begin(dev); gf->draw_lc(dev, glx + bandIcon + snap(4.0f * S), gcy, gs, gsz, plr_col(PLR_GIL, 0xFFFFE48Cu), 0xFF000000u, plr_ow(PLR_GIL, 1.4f * S)); }
        }
        // text overlays (one font pass) : id fallback for missing-BMP slots + the ammo stack count.
        Font* ef = f.font;
        if (ef) {
            bool begun = false;
            // id-text fallback for occupied slots whose BMP is missing (un-bundled / brand-new item)
            for (int s = 0; s < 16; ++s) {
                if (gearTex_[s] || eq.id[s] == 0 || eq.id[s] == 0xFFFF) continue;
                const int dp = EQ_DPOS[s];
                const float cx = snap(gx0 + (dp % 4) * eqCell), cy = snap(gy0 + (dp / 4) * eqCell);
                if (!begun) { ef->begin(dev); begun = true; }
                char idb[8]; sprintf(idb, "%u", eq.id[s]);
                ef->draw_c(dev, snap(cx + cellSz * 0.5f), snap(cy + cellSz * 0.5f), idb, snap(9.0f * S), 0xFFDDE6F2u, 0xFF000000u, 1.0f);
            }
            // ammo stack count (slot 3) : bottom-right of the ammo cell when the stack is > 1
            if (eq.count[3] > 1 && eq.id[3] != 0 && eq.id[3] != 0xFFFF) {
                const int dp = EQ_DPOS[3];
                const float cx = snap(gx0 + (dp % 4) * eqCell), cy = snap(gy0 + (dp / 4) * eqCell);
                char cb[8]; sprintf(cb, "%u", eq.count[3]);
                const float asz = snap(11.0f * S), aw = ef->measure(cb, asz), m = snap(2.0f * S);
                if (!begun) { ef->begin(dev); begun = true; }
                ef->draw_lc(dev, snap(cx + cellSz - aw - m), snap(cy + cellSz - asz * 0.5f - m), cb, asz, 0xFFFFFFFFu, 0xFF000000u, 1.8f * S);
            }
        }
    }
}

} // namespace aio
