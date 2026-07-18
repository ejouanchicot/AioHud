// party.h -- the party list (XivParty-style), faithful to the mockup look.
//
// Fully DIRECT D3D8: panel / job badge / the three HP-MP-TP gauges are colour-quads
// (vertical-gradient fills + gloss + coloured borders), and the text (name, job/sub, cast,
// gauge values) is drawn through Frame::fonts -- each element (name / bars / badge) picks its
// OWN face+weight atlas so they can be styled independently. Rows are LIVE: self from game
// memory + the other members (incl. trusts) from the live party array, with demo data as a
// fallback when out of game. Look mirrors design/src/panels/party.css.
#pragma once
#include <string>
#include "ui/widget.h"
#include "model/ui_config.h"   // gauge H/W scales (barHeight / barWidth)

namespace aio {

// One real HP/MP/TP liquid gauge (the exact party-row renderer), exposed so the Help can show live
// examples. pct 0..100 ; col = fill colour ; t = time ; pulse = WS-ready glow ; danger = critical-HP blink.
void party_gauge(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger = 0.0f, int kind = -1, int style = 0);
// The REAL selection hand (tex from the Party widget), exposed so the Help can show it.
// mode: 0 = main (white), 1 = sub-target (blue), 2 = locked-on (red).
void party_cursor(u32 dev, u32 tex, float cx, float cy, float size, int mode);
// The REAL selection frame (gold glass main / ocean-blue sub / red locked) with its moving glass sweep, for the Help.
// mode: 0 = main, 1 = sub-target, 2 = locked-on.
void party_selframe(u32 dev, float x, float y, float w, float h, float t, float alpha, int mode);
// The cursor's horizontal bob offset (px) for a time + icon size, on the same rhythm as the live rows.
float party_cursor_bob(float t, float size);
// CONFIG-PREVIEW ONLY : clamp the LEFTWARD member buff strip to this stage-left X (px) ; icons past it collapse
// into a "+N" marker at the leftmost fitting cell. 0 = live HUD (no cap ; every buff drawn as in game).
void set_party_preview_buff_left(float x);

class Party : public Widget {
public:
    const char* type_name() const override { return "PartyList"; }
    void configure(const json::Value& cfg) override;        // per-text style + demo data
    void measure(float& w, float& h) const override;
    void ensure(u32 dev) override;           // create the AA dot texture (leader/QM bullets)
    void on_device_lost() override;          // forget the dot handle (zoning)
    void dispose() override;                  // release the dot texture (unload)
    void draw(const Frame& f) override;
    int  tier() const { return tier_; }      // 0 = main party, 1/2 = alliance boxes (used by the config preview)
    float buff_reserve_w() const { return buffReserveW_; }   // last-drawn width of the left buff strip (0 for alliances) -> the preview centres the box+buffs cluster
    int  tcfg() const { return tier_ == 0 ? 0 : 1; }   // config GROUP : 0 = Party, 1 = Alliance (both alliance boxes share one config)
    u32  cursor_tex() const { return icon_tex_; }   // the loaded selection-hand texture (for the Help live sample)

private:
    void row(int i, void* out) const;        // fill one demo Row ; void* keeps Row out of the header
    void demo_row(int i, void* out) const;   // forced demo row (tier-offset names), //aio demo
    int  build_rows(void* rows, const GameState& gs) const;   // fill rows[] from the per-frame snapshot (or demo) -> count
    float box_w_base() const;                // box width (base px) : AUTO-fit to the columns + name size
    // the floating spell / job-ability / weapon-skill info box (MP cost / recast "Next" / live TP),
    // pulled out of draw() : a self-contained feature that doesn't iterate the rows.
    void draw_action_box(const Frame& f, float S, float px, float w, float oy, Font* fName, u32 nSTK, float nOWf);

    // per-member animation state, persisted across frames (lerped bars + dot pop). Keyed by id.
    struct RowAnim { unsigned id; float hpp, mpp, tpp; float dot[3]; bool seen; };
    RowAnim* anim_for(unsigned id);          // find-or-allocate the anim slot for a member
    RowAnim  anim_[8] = {};
    float    lastT_ = -1.0f;                 // previous frame time (for dt-based smoothing)
    unsigned selId_ = 0;                     // member id currently targeted (<t>)
    float    selY_  = 0.0f;                  // animated selection-cursor top Y (px) -> slides between rows
    float    selA_  = 0.0f;                  // selection fade 0..1
    float    selZoom_ = 0.0f;                // selected-row zoom (0->1) : grows in on target, HOLDS while targeted
    unsigned subId_ = 0;                     // member id currently SUB-targeted (<st>)
    float    subY_  = 0.0f;                  // animated sub-target bar top Y (px) -> slides between rows
    float    subA_  = 0.0f;                  // sub-target fade 0..1
    float    subZoom_ = 0.0f;                // sub-target name zoom (0->1), mirrors selZoom_
    int      menuHold_ = 0;                  // debounce : frames left to keep the action-menu box up
    int      menuType_ = 0;                  // 1 = spell, 2 = job ability, 3 = weapon skill
    unsigned menuSpell_ = 0;                 // last highlighted action id (spell / ability / WS)
    unsigned prevAbRaw_ = 0;                 // previous frame's RAW ability examine -> a change = the game just
                                             // examined a real ability (live : auto-select on open, or navigation)
    int      menuRawPrev_ = 0;               // previous frame's raw menu type -> detect a FRESH menu opening
    int      menuPrevCur_ = -1;              // previous menu cursor index -> detect a moved cursor (ghost check)
    bool     menuLive_ = false;              // is the current examine a REAL selection vs a stale ghost ?
    float    selBobT0_ = 0.0f;               // bob phase origin -> reset on target change so the cursor restarts centred

    u32 dot_tex_ = 0;                         // shared white AA disc, tinted per marker
    u32 icon_tex_ = 0;                        // selection-cursor icon (hand pointing right), loaded from assets
    bool icon_tried_ = false;                 // attempted to load icon_tex_ (don't retry the file every frame)
    u32 buff_tex_ = 0;                        // status-icon atlas (buffs drawn to the left of each party row)
    bool buff_tried_ = false;                 // attempted to load buff_tex_ (don't retry the file every frame)
    u32 jobicon_tex_ = 0;                     // job-emblem atlas (white masks, tinted per role ; job badge "Icons" mode)
    bool jobicon_tried_ = false;              // attempted to load jobicon_tex_

    static const int MAXM = 6;
    int count_ = 6;
    int tier_  = 0;                                        // 0 = main party box, 1 = alliance1, 2 = alliance2
    mutable float buffReserveW_ = 0.0f;                    // width of the left buff strip from the last draw (0 for alliances)
    int dhp_[MAXM] = { 100, 70, 18, 45, 0, 95 };            // % (0..100)  (overridable by config)
    int dmp_[MAXM] = {  42, 73,  0, 60, 0, 40 };            // %
    int dtp_[MAXM] = { 3000, 1000, 2000, 300, 0, 600 };     // 0..3000
    int dbuff_[MAXM] = { 3, 7, 22, 6, 1, 5 };               // demo buff COUNT per member (22 -> capped at 20) ; cfg p%d_buffs

    // --- live-tunable style : 3 independent text elements (name / bars / badge), each with
    //     its own size, outline, weight (bold) and font face. Everything else (box width,
    //     row height, badge & gauge sizes) AUTO-derives. Empty *Font = the global face. ---
    float nameSz_  = 10.0f;  float nameStroke_  = 0.8f;  bool nameBold_  = false;  std::string nameFont_;
    float barSz_   =  8.4f;  float barStroke_   = 0.8f;  bool barBold_   = false;  std::string barFont_;
    float badgeSz_ =  8.5f;  float badgeStroke_ = 0.8f;  bool badgeBold_ = false;  std::string badgeFont_;

    // derived geometry (base px, pre-scale) -- box & badge adapt to their content:
    float subSz()    const { return badgeSz_ * 0.76f; }
    float castSz()   const { return nameSz_  * 1.0f; }
    float badgeW()   const { int m = ui_config().jobBadge[tcfg()]; float s = ui_config().text[tcfg()][TE_BADGE].size * badgeSc(); return m == 0 ? 0.0f : (m == 3 ? badgeSz_ * 2.1f * s + 6.0f : badgeSz_ * 1.9f * s + 8.0f); }   // 0 = off -> column collapses ; 3 = square icon ; grows with the badge text size * per-box Badge Size
    float badgeH()   const { int m = ui_config().jobBadge[tcfg()]; float s = ui_config().text[tcfg()][TE_BADGE].size * badgeSc(); return m == 0 ? 0.0f : (m == 3 ? badgeSz_ * 2.1f * s + 6.0f : (m == 1 ? badgeSz_ * s * 0.76f : (badgeSz_ + subSz()) * s * 0.86f)); }   // 3 = square icon ; text modes : box sized to the glyph INK (caps/digits ~0.7em), not the full em -> hugs the text
    float badgeSc()  const { float b = ui_config().badgeScale[tcfg()]; return b < 0.5f ? 0.5f : (b > 2.0f ? 2.0f : b); }   // per-box Badge Size % (0.50 .. 2.00)
    // base cell fits ~4 digits at 100% WITH slack, so a bigger value text first uses that slack ; the gauge
    // only widens once the number at its size would actually exceed the cell (not immediately above 100%).
    float gaugeW()   const {
        float vm = ui_config().text[tcfg()][TE_HP].size;
        if (ui_config().text[tcfg()][TE_MP].size > vm) vm = ui_config().text[tcfg()][TE_MP].size;
        if (ui_config().text[tcfg()][TE_TP].size > vm) vm = ui_config().text[tcfg()][TE_TP].size;
        float cell = barSz_ * 2.6f;                 // base cell (4 digits + slack)
        float need = barSz_ * 2.25f * vm;           // 4-digit width at the value size
        if (need > cell) cell = need;               // grow ONLY past the slack
        return (cell + 6.0f) * ui_config().barWidth[tcfg()];
    }
    // round styles (Sphere/Ring/Crystal = 4/5/6) use a SQUARE cell so the disc is big enough to hold the
    // value -> the row grows to fit. Vial/Bars/Segments/Minimal/Text keep the flat height.
    bool  circularGauge() const { int s = ui_config().gaugeStyle[tcfg()]; return s >= 4 && s <= 6; }
    float gaugeH()   const { return circularGauge() ? gaugeW() : (barSz_ + 6.0f) * ui_config().barHeight[tcfg()]; }   // height-scalable, per box
    float gaugeGap() const { return 3.0f; }
    // holds up to ~3 leader/QM dots ; when the distance is shown it must also fit "00.00" at ITS font size,
    // so the column (and the whole box) GROWS with the Distance text size instead of the number being capped.
    float marksW()   const {
        float w = 20.0f;
        if (distOn()) { float d = badgeSz_ * 3.4f * ui_config().text[tcfg()][TE_DIST].size; if (d > w) w = d; }
        return w;
    }
    float padB()     const { return 4.0f; }   // top/bottom inner margin -> rows + selection frame stay off the box border
    bool  distOn()   const { return ui_config().dist[tcfg()]; }   // tcfg() : both alliance boxes share the Alliance config (was dist[tier_] -> Alliance 2 ignored the toggle)
    // height of the marks column : leader/QM pips on TOP + (when shown) the distance number below. Used as
    // a FLOOR for the main band so they never touch ; when the distance is OFF only the pips need room ->
    // the floor drops and the row can be more compact.
    float marksColH() const { return distOn() ? (8.0f + badgeSz_ * 1.20f * ui_config().text[tcfg()][TE_DIST].size) : 8.0f; }
    // BUFF strip (party only, left of the row) : the icon size is driven by the Buff Size % (0.40..2.00) off a
    // stable base -> pushing it bigger GROWS the row height so the icons fit. The height ALWAYS reserves TWO
    // rows so the party size stays constant whatever Max Buffs is (Max Buffs only caps the count shown). Base px.
    float buffIconBase() const { float bf = ui_config().buffScale; if (bf < 0.40f) bf = 0.40f; if (bf > 2.0f) bf = 2.0f; return barSz_ * bf; }
    int   buffRows()     const { int r = ui_config().buffRows; return (r <= 1) ? 1 : 2; }   // 1 or 2 rows (config)
    // per-icon height : two-row = the base size ; ONE-row = a ~full-player-line-tall icon (so one line fills the row).
    float buffIconH()    const { return (buffRows() == 1) ? (buffIconBase() * 1.9f) : buffIconBase(); }
    float buffBandH()    const { return (float)buffRows() * buffIconH() + (float)(buffRows() - 1) * 1.0f; }
    // MAIN BAND : height of the primary line where badge / name / gauges / distance / marks all centre
    // together (tallest of them). The cast/spell line sits BELOW this band.
    // the badge/name/gauge/marks LINE height -> the selection-cursor size reference. NOT inflated by the party buff
    // band, so the cursor stays proportional to the name it points at, on the SAME basis for party and alliance.
    float coreBandH() const {
        float m = badgeH(); float v = gaugeH(); if (v > m) m = v;
        float n = nameSz_ * ui_config().text[tcfg()][TE_NAME].size + 2.0f; if (n > m) m = n;   // name grows with its text size
        float k = marksColH();   if (k > m) m = k;
        return m;
    }
    float mainBandH() const {
        float m = coreBandH();
        if (tier_ == 0) { float b = buffBandH() + buffIconBase() * 0.6f; if (b > m) m = b; }   // party buffs GROW the row (+ margin so two-row buffs stay clear of the next player)
        return m;
    }
    // row = half the main band (the name centres in it) + half the name + the cast line + a gap, so the
    // cast sits just under the name with clear air. When casts are OFF for this box type the cast line is
    // NOT reserved -> compact rows (= just the main band). Both feed measure() -> the box re-fits + the
    // bottom-right re-anchors (HUD).
    bool  castOn()   const { return ui_config().cast[tcfg()]; }
    float rowH()     const { return castOn() ? (mainBandH() * 0.5f + nameSz_ * 0.5f + castSz() + 6.0f) : (mainBandH() + 2.0f); }
    float rowPit()   const { return rowH() + 1.0f; }
};

} // namespace aio
