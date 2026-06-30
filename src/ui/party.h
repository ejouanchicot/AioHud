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
    float    selBobT0_ = 0.0f;               // bob phase origin -> reset on target change so the cursor restarts centred

    u32 dot_tex_ = 0;                         // shared white AA disc, tinted per marker
    u32 icon_tex_ = 0;                        // selection-cursor icon (hand pointing right), loaded from assets
    bool icon_tried_ = false;                 // attempted to load icon_tex_ (don't retry the file every frame)
    u32 buff_tex_ = 0;                        // status-icon atlas (buffs drawn to the left of each party row)
    bool buff_tried_ = false;                 // attempted to load buff_tex_ (don't retry the file every frame)

    static const int MAXM = 6;
    int count_ = 6;
    int tier_  = 0;                                        // 0 = main party box, 1 = alliance1, 2 = alliance2
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
    float badgeW()   const { return ui_config().jobBadge == 0 ? 0.0f : (badgeSz_ * 1.9f + 8.0f); }   // 0 = off -> column collapses
    float badgeH()   const { int m = ui_config().jobBadge; return m == 0 ? 0.0f : (m == 1 ? badgeSz_ + 4.0f : badgeSz_ + subSz() + 4.0f); }   // main-only is shorter
    float gaugeW()   const { return (barSz_  * 2.6f + 6.0f) * ui_config().barWidth; }   // ~4-digit value, width-scalable
    float gaugeH()   const { return (barSz_  + 6.0f) * ui_config().barHeight; }          // height-scalable (uses the taller-row room)
    float gaugeGap() const { return 3.0f; }
    float marksW()   const { return 20.0f; }   // holds up to ~3 leader/QM dots, centred -> badge stays clear
    float padB()     const { return 4.0f; }   // top/bottom inner margin -> rows + selection frame stay off the box border
    bool  distOn()   const { int t = tier_ < 0 ? 0 : (tier_ > 2 ? 2 : tier_); return ui_config().dist[t]; }   // show distance for this box
    // height of the marks column : leader/QM pips on TOP + (when shown) the distance number below. Used as
    // a FLOOR for the main band so they never touch ; when the distance is OFF only the pips need room ->
    // the floor drops and the row can be more compact.
    float marksColH() const { return distOn() ? (8.0f + badgeSz_ * 1.20f) : 8.0f; }
    // MAIN BAND : height of the primary line where badge / name / gauges / distance / marks all centre
    // together (tallest of them). The cast/spell line sits BELOW this band.
    float mainBandH() const {
        float m = badgeH(); float v = gaugeH(); if (v > m) m = v;
        float n = nameSz_ + 2.0f; if (n > m) m = n;
        float k = marksColH();   if (k > m) m = k;
        return m;
    }
    // row = half the main band (the name centres in it) + half the name + the cast line + a gap, so the
    // cast sits just under the name with clear air. When casts are OFF for this box type the cast line is
    // NOT reserved -> compact rows (= just the main band). Both feed measure() -> the box re-fits + the
    // bottom-right re-anchors (HUD).
    bool  castOn()   const { return (tier_ == 0) ? ui_config().castParty : ui_config().castAlly; }
    float rowH()     const { return castOn() ? (mainBandH() * 0.5f + nameSz_ * 0.5f + castSz() + 6.0f) : (mainBandH() + 2.0f); }
    float rowPit()   const { return rowH() + 1.0f; }
};

} // namespace aio
