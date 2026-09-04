// hud_preview.cpp -- Hud::draw_config_preview, split out of hud.cpp (PURE MOVE).
//
// This is config-panel logic, not compositor logic : it draws the REAL widgets into the //aio config
// page's preview stage. It lived in hud.cpp only because the Hud owns the widget instances. Every other
// module already has its own hud_*.cpp ; this follows that split. Behaviour is unchanged.
#include "hud.h"
#include "ui/hud_internal.h"
#include "ui/factory.h"
#include "ui/player.h"
#include "ui/party.h"
#include "ui/target.h"
#include "ui/minimap.h"
#include "model/gamestate.h"
#include "model/party_state.h"
#include "model/ui_config.h"
#include "ui/box_style.h"
#include "ui/text_style.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/paths.h"
#include "gfx/texture.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace aio {

// the detached-Debuffs free renderer (defined in hud_debuffs.cpp) : measureOnly to size it for preview placement.
void debuffs_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                  u32 buffAtlas, bool measureOnly, float* outW, float* outH);

// Draw the REAL party + 2-alliance demo boxes into the config page's preview stage. Runs AFTER
// config_.draw so the boxes sit on top of the page. We force //aio alliance2 demo for the duration
// and temporarily place the party box bottom-right at the stage anchor (the alliance boxes + cost-box
// space stack above it via the normal logic), then restore every value we touched.
void Hud::draw_config_preview(const Frame& f) {
    // HELP tab : the Player-box sample -> draw the REAL demo Player box (identity / vitals / buffs / gil / speed /
    // equipment) in the slot the Help reserved, scaled to fit. The Hud owns the widget, so this can't live in ConfigPage.
    { float pcx = 0.0f, pcy = 0.0f, pW = 0.0f, pH = 0.0f;
      if (config_.help_player_slot(pcx, pcy, pW, pH)) {
          Player* pl = nullptr;
          for (size_t i = 0; i < widgets_.size(); ++i)
              if (strcmp(widgets_[i]->type_name(), "PlayerHub") == 0) { pl = static_cast<Player*>(widgets_[i]); break; }
          if (pl) {
              const float osx = pl->px(), osy = pl->py(), oscale = pl->scale();
              float tw = 0, th = 0, ml = 0, mt = 0, mr = 0, mb = 0;
              pl->preview_footprint(tw, th, ml, mt, mr, mb);                       // footprint at the real scale
              float k = 1.2f;
              if (tw > 1.0f && pW > 1.0f) { const float kw = pW / tw; if (kw < k) k = kw; }
              if (th > 1.0f && pH > 1.0f) { const float kh = pH / th; if (kh < k) k = kh; }   // fit the slot, cap upscale at 1.2
              pl->set_scale(oscale * k);
              float tw2 = 0, th2 = 0; pl->preview_footprint(tw2, th2, ml, mt, mr, mb);
              pl->set_origin((float)(int)(pcx - tw2 * 0.5f + 0.5f), (float)(int)(pcy - th2 * 0.5f + 0.5f));
              pl->set_demo(true);
              pl->draw(f);
              pl->set_demo(false);
              pl->set_origin(osx, osy);
              pl->set_scale(oscale);
          }
      }
    }
    float rightX = 0.0f, bottomY = 0.0f;
    if (!config_.preview_anchor(rightX, bottomY)) return;

    // The live preview reflects the master Show : if the current module is OFF, draw a "hidden" note in the
    // stage (so you see it won't appear in game) instead of its demo.
    auto draw_hidden_note = [&]() {
        float sx = 0, sy = 0, sw = 0, sh = 0; if (!config_.preview_rect(sx, sy, sw, sh)) return;
        Font* pf = fonts_.get(ui_font_face(ui_config().fontFace), 700);
        if (!pf) return;
        pf->begin(f.dev);
        pf->draw_c(f.dev, sx + sw * 0.5f, sy + sh * 0.5f,
                   (ui_config().lang == 1) ? "Module masqu\xC3\xA9  (Afficher : Non)" : "Module hidden  (Show: Off)",
                   (screenH_ / 1000.0f) * 22.0f, 0xFFCED6DB, 0xC0000000, 1.2f);
    };
    { const UiConfig& CC = ui_config(); bool off = false;
      switch (config_.section()) {
          case 1:  off = !CC.tgtShow;  break;  case 2:  off = !CC.plrShow;  break;
          case 3:  off = !CC.mmShow;   break;  case 4:  off = !CC.wsShow;   break;
          case 5:  off = !CC.scShow;   break;  case 6:  off = !CC.tpShow;   break;
          case 7:  off = !CC.hlShow;   break;  case 8:  off = !CC.pwShow;   break;
          case 9:  off = !CC.grimShow; break;  case 10: off = !CC.ztShow;   break;
          case 11: off = !CC.tmShow;   break;  case 12: off = !CC.epShow;   break;
          default: break; }
      if (off) { draw_hidden_note(); return; }
    }

    // Target module page -> preview the TARGET box (a demo target) as a MINI-MAP of the real screen : the box's
    // on-screen placement (centre-lock / dragged fraction / layout default) maps into the stage, so toggling
    // Centre H/V or dragging visibly moves it here.
    if (config_.section() == 1) {
        Target* tg = nullptr;
        for (size_t i = 0; i < widgets_.size(); ++i)
            if (strcmp(widgets_[i]->type_name(), "TargetBar") == 0) { tg = static_cast<Target*>(widgets_[i]); break; }
        if (!tg) return;
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        const float osx = tg->px(), osy = tg->py();
        float tw = 0.0f, th = 0.0f; tg->measure(tw, th);
        const float bx = sx + (sw - tw) * 0.5f;
        const bool stackDb = ui_config().tgtDebuffs && ui_config().dbShow;
        // With a detached Debuffs box, centre the target+debuff GROUP vertically (not each pinned to an edge).
        // Use the target's FULL last-drawn height (measure() reports only name+bar) + the measured debuff height.
        float by = sy + (sh - th) * 0.5f;   // default : target centred
        float liveS = 0.0f, dh = 0.0f, gap = 14.0f;
        if (stackDb) {
            float dsc = ui_config().dbScale; if (dsc < 0.5f) dsc = 0.5f; if (dsc > 2.0f) dsc = 2.0f;
            liveS = (screenH_ / 1000.0f) * dsc;
            float dw = 0.0f; debuffs_draw(f, true, 0.0f, 0.0f, liveS, (float)screenW_, (float)screenH_, buffAtlas_, true, &dw, &dh);
            const float realTh = tg->drawn_height();                     // full target height (last frame ; settles in 1)
            by = sy + (sh - (realTh + gap + dh)) * 0.5f; if (by < sy + 6.0f) by = sy + 6.0f;   // centre the GROUP
        }
        tg->set_origin((float)(int)(bx + 0.5f), (float)(int)(by + 0.5f));
        tg->set_demo(true);
        tg->draw(f);
        tg->set_demo(false);
        tg->set_origin(osx, osy);   // restore real placement
        if (stackDb)   // debuff below the target's REAL bottom (this frame's height) -> centred group, no overlap
            draw_debuffs(f, true, sx + sw * 0.5f, by + tg->drawn_height() + gap + dh * 0.5f, liveS);
        return;
    }

    // Player Hub page -> preview the hub (demo player) as a mini-map of its screen position, like the Target box.
    if (config_.section() == 2) {
        Player* pl = nullptr;
        for (size_t i = 0; i < widgets_.size(); ++i)
            if (strcmp(widgets_[i]->type_name(), "PlayerHub") == 0) { pl = static_cast<Player*>(widgets_[i]); break; }
        if (!pl) return;
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        const float osx = pl->px(), osy = pl->py();
        // REAL footprint (incl. in-box equipment grid) + docked-grid margins -> position so nothing spills the preview.
        float tw = 0.0f, th = 0.0f, ml = 0.0f, mt = 0.0f, mr = 0.0f, mb = 0.0f;
        pl->preview_footprint(tw, th, ml, mt, mr, mb);
        // DETACHED equipment -> show the Hub + the standalone grid as a centred GROUP, stacked VERTICALLY
        // (Hub on top, grid below), like the Target page stacks the detached debuffs under the target. One
        // draw() paints both (the grid at the forced pvEq spot).
        if (ui_config().plrEquip && ui_config().plrEquipDetach) {
            float ew = 0.0f, eh = 0.0f; pl->equip_footprint(ew, eh);
            const float gap = 16.0f, groupW = (tw > ew ? tw : ew), groupH = th + gap + eh;
            float gx = sx + (sw - groupW) * 0.5f; if (gx < sx + 6.0f) gx = sx + 6.0f;
            float gy = sy + (sh - groupH) * 0.5f; if (gy < sy + 6.0f) gy = sy + 6.0f;
            const float hubX = gx + (groupW - tw) * 0.5f, eqX = gx + (groupW - ew) * 0.5f, eqY = gy + th + gap;
            pl->set_origin((float)(int)(hubX + 0.5f), (float)(int)(gy + 0.5f));
            pl->set_eq_preview(true, (float)(int)(eqX + 0.5f), (float)(int)(eqY + 0.5f));
            pl->set_demo(true);
            pl->draw(f);
            pl->set_demo(false);
            pl->set_eq_preview(false);
            pl->set_origin(osx, osy);
            return;
        }
        // ALWAYS centred in the stage, within the docked-grid margins so a side equipment grid never spills.
        const float bx = sx + ml + (sw - ml - mr - tw) * 0.5f;
        const float by = sy + mt + (sh - mt - mb - th) * 0.5f;
        pl->set_origin((float)(int)(bx + 0.5f), (float)(int)(by + 0.5f));
        pl->set_demo(true);
        pl->draw(f);
        pl->set_demo(false);
        pl->set_origin(osx, osy);   // restore real placement
        return;
    }

    // Minimap page -> preview the real minimap CENTRED in the stage (it reads live game data, so it shows the
    // current zone + entities). We force a temporary screen-fraction position over the stage centre and restore
    // it after -- the widget owns no demo mode, its position is the only thing we steer.
    if (config_.section() == 3) {
        Minimap* mm = nullptr;
        for (size_t i = 0; i < widgets_.size(); ++i)
            if (strcmp(widgets_[i]->type_name(), "Minimap") == 0) { mm = static_cast<Minimap*>(widgets_[i]); break; }
        if (!mm) return;
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        UiConfig& C = ui_config();
        const float scW = f.screenW > 0.0f ? f.screenW : 1920.0f, scH = f.screenH > 0.0f ? f.screenH : 1080.0f;
        float ms = C.mmScale; ms = ms < 0.5f ? 0.5f : (ms > 2.0f ? 2.0f : ms);
        const float w = 220.0f * ui_scale_ * ms;                       // the minimap footprint (square) -- matches Minimap::draw's W = 220*scale_*mmScale
        const float bx = sx + (sw - w) * 0.5f, by = sy + (sh - w) * 0.5f;
        const bool  sPosSet = C.mmPosSet; const float sX = C.mmX, sY = C.mmY;
        C.mmPosSet = true; C.mmX = bx / scW; C.mmY = by / scH;         // place it over the stage centre for this draw
        C.mmPreview = true;                                           // tell Minimap::draw NOT to persist this temp position
        mm->draw(f);
        C.mmPreview = false;
        C.mmPosSet = sPosSet; C.mmX = sX; C.mmY = sY;                  // restore the real placement
        return;
    }

    // Arcade WS module -> a LOOPING sample popup CENTRED IN THE PREVIEW STAGE (scaled to fit), on top of the page.
    if (config_.section() == 4) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        const float fitUS = sw > 0.0f ? (sw / 620.0f) : (screenH_ / 1400.0f);   // fit the ~600px popup into the stage width
        draw_ws_popup(f, true, sx + sw * 0.5f, sy + sh * 0.5f, fitUS);
        return;
    }
    // Skillchains module -> a LOOPING sample box centred in the preview stage. Reflect the user's Size so the
    // slider visibly changes the box here (the base fit keeps it inside the stage).
    if (config_.section() == 5) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().scScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        // Resolution-relative so the WHOLE 50-200% Size range stays visible (a fixed cap clamped early on tall
        // preview stages -> the box looked frozen past ~75%). base = sh/700 -> at 200% fitS = sh/350, still below
        // the "fills the stage" cap sh/330, so it never clamps inside the range.
        const float fmax = (sh > 0.0f) ? (sh / 330.0f) : 2.5f;
        float fitS = (sh > 0.0f ? (sh / 700.0f) : 0.5f) * scl; if (fitS < 0.30f) fitS = 0.30f; if (fitS > fmax) fitS = fmax;
        draw_skillchains(f, true, sx + sw * 0.5f, sy + sh * 0.5f, fitS);
        return;
    }
    // Treasure Pool module -> a sample pool centred in the preview stage (Size reflected).
    if (config_.section() == 6) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().tpScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        // Resolution-relative (same fix as Skillchains) so the whole 50-200% Size range stays visible : base
        // sh/840 -> at 200% fitS = sh/420 (the old "fills the stage" scale), kept under the cap sh/400. No early clamp.
        const float fmax = (sh > 0.0f) ? (sh / 400.0f) : 2.5f;
        float fitS = (sh > 0.0f ? (sh / 840.0f) : 0.5f) * scl; if (fitS < 0.30f) fitS = 0.30f; if (fitS > fmax) fitS = fmax;
        draw_treasure_pool(f, true, sx + sw * 0.5f, sy + sh * 0.5f, fitS);
        return;
    }
    // Hate List module -> the sample list at its TRUE in-game size (WYSIWYG : ovS = the exact live scale
    // screenH/1000 * Size), centred in the preview stage -> what you see here is what you get on the HUD.
    if (config_.section() == 7) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().hlScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        const float liveS = (screenH_ / 1000.0f) * scl;
        draw_hate_list(f, true, sx + sw * 0.5f, sy + sh * 0.5f, liveS);
        return;
    }
    // PointWatch module -> the sample bars at their TRUE in-game size (WYSIWYG), centred in the preview stage.
    if (config_.section() == 8) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().pwScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        const float liveS = (screenH_ / 1000.0f) * scl;
        draw_pointwatch(f, true, sx + sw * 0.5f, sy + sh * 0.5f, liveS);
        return;
    }
    // Grimoire module -> the book (Addendum White by default) at TRUE in-game size, centred in the preview stage.
    if (config_.section() == 9) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().grimScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        const float liveS = (screenH_ / 1000.0f) * scl;
        draw_grimoire(f, true, sx + sw * 0.5f, sy + sh * 0.5f, liveS);
        return;
    }
    // Zone Tracker module -> the chosen zone variant at TRUE in-game size, centred in the preview stage.
    if (config_.section() == 10) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().ztScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        const float liveS = (screenH_ / 1000.0f) * scl;
        draw_zonetracker(f, true, sx + sw * 0.5f, sy + sh * 0.5f, liveS);
        return;
    }
    // Timers module -> the sample two-column box at TRUE in-game size, centred in the preview stage (WYSIWYG).
    if (config_.section() == 11) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().tmScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        const float liveS = (screenH_ / 1000.0f) * scl;
        draw_timers(f, true, sx + sw * 0.5f, sy + sh * 0.5f, liveS);
        return;
    }
    // EmpyPop module -> the built-in chloris sample chain at TRUE in-game size, centred in the preview stage (WYSIWYG).
    if (config_.section() == 12) {
        float sx = 0, sy = 0, sw = 0, sh = 0; config_.preview_rect(sx, sy, sw, sh);
        float scl = ui_config().epScale; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
        const float liveS = (screenH_ / 1000.0f) * scl;
        draw_empypop(f, true, sx + sw * 0.5f, sy + sh * 0.5f, liveS);
        return;
    }

    // Party page : both sides off -> hidden note. Otherwise draw only the sides that are ON (per-tier below).
    if (!ui_config().partyShow && !ui_config().allyShow) { draw_hidden_note(); return; }

    Party* tiers[3] = { nullptr, nullptr, nullptr };
    for (size_t i = 0; i < widgets_.size(); ++i) {
        Widget* w = widgets_[i];
        if (strcmp(w->type_name(), "PartyList") != 0) continue;   // PartyList + AllianceList both => Party
        Party* p = static_cast<Party*>(w);
        int t = p->tier();
        if (t >= 0 && t < 3) tiers[t] = p;
    }
    if (!tiers[0]) return;

    UiConfig& C = ui_config();
    const int   savedLvl = party_demo_level(), savedCnt = party_demo_count();
    const bool  sp0 = C.box[0].posSet, sp1 = C.box[1].posSet, sp2 = C.box[2].posSet;
    float sref[6]; for (int i = 0; i < 6; ++i) sref[i] = C.partyRef[i];
    const float sbot = C.partyBottomY;
    float sally[4]; for (int i = 0; i < 4; ++i) sally[i] = C.allyRefY[i];
    const float opx[3] = { tiers[0]->px(), tiers[1] ? tiers[1]->px() : 0.0f, tiers[2] ? tiers[2]->px() : 0.0f };
    const float opy[3] = { tiers[0]->py(), tiers[1] ? tiers[1]->py() : 0.0f, tiers[2] ? tiers[2]->py() : 0.0f };

    set_party_demo_level(3);                            // party + alliance 1 + alliance 2
    set_party_demo_count(6);                            // full 6-member party for a representative preview
    // use px_/py_ for ALL three (not the user's box overrides) so they SHARE the same right edge :
    // the party sits at the anchor, the alliances align to it and stack upward (cost-box space included).
    C.box[0].posSet = false; C.box[1].posSet = false; C.box[2].posSet = false;
    for (int i = 0; i < 6; ++i) C.partyRef[i] = -1.0f;
    C.partyBottomY = -1.0f;
    for (int i = 0; i < 4; ++i) C.allyRefY[i] = -1.0f;   // preview uses the stacked layout, not the markers

    float pw = 0.0f, ph = 0.0f; tiers[0]->measure(pw, ph);   // party box footprint (bottom-anchored, full 6 rows)
    float tmw = 0.0f, ah1 = 0.0f, ah2 = 0.0f;                // alliance box heights (they stack ABOVE the party)
    if (tiers[1]) tiers[1]->measure(tmw, ah1);
    if (tiers[2]) tiers[2]->measure(tmw, ah2);
    (void)rightX; (void)bottomY;
    float psx = 0.0f, psy = 0.0f, psw = 0.0f, psh = 0.0f; config_.preview_rect(psx, psy, psw, psh);
    const float stackH = ph + ah1 + ah2 + ph * 0.14f;        // + rough Cost/Next box reserve above the party
    // SNAP the box origin to whole pixels : measure() is fractional, so an un-snapped origin puts the
    // whole box (badge, name, gauges) on sub-pixel coords -> the first glyph's left column gets eaten by
    // filtering ONLY in the preview (live uses an integer layout origin). This is the truncation cause.
    // Centre the WHOLE CLUSTER -- the box plus the buff strip that hangs off its left edge. Centring the box
    // alone kept it from shifting when Max Buffs changed, which is a stability nobody asked for and which cost
    // the thing everyone sees: the strip pushes the visual mass left, so a box centred on its own footprint
    // sits visibly right of centre. buff_reserve_w() is the width the strip really drew last frame, and it was
    // exposed for exactly this.
    // CLAMPED to what the stage can actually show. buff_reserve_w() is the width the strip RESERVES, and the
    // preview then clips that strip at the stage's left edge (set_party_preview_buff_left, with a "+N" for
    // the rest). Centring on the reserved width therefore centred on something wider than what gets drawn,
    // and the whole cluster slid right as soon as Buff Size grew -- the exact symptom. Centre on what will
    // be VISIBLE : the cluster can then never overflow the stage, and the strip drops to "+N" instead.
    float buffW = tiers[0]->buff_reserve_w();
    const float buffRoom = psw - pw - snap(12.0f);
    if (buffW > buffRoom) buffW = (buffRoom > 0.0f) ? buffRoom : 0.0f;
    const float boxX     = (float)(int)(psx + (psw - (pw + buffW)) * 0.5f + buffW + 0.5f);
    const float partyTop = (float)(int)(psy + (psh + stackH) * 0.5f - ph + 0.5f);    // centre the whole stack vertically
    for (int t = 0; t < 3; ++t) if (tiers[t]) tiers[t]->set_origin(boxX, partyTop);   // shared X ; party Y anchors the stack
    // PREVIEW-ONLY : the party box keeps its REAL size, but its buff strip is drawn LEFTWARD and a long strip
    // (high Max Buffs / 1-row) would spill past the stage's LEFT edge. Tell the party box to CAP the strip at
    // the stage left (+ a small margin) and draw a "+N" marker for the buffs that didn't fit. Live HUD passes
    // 0 (no cap) -> every buff still draws in game. Reset right after the party draw below.
    set_party_preview_buff_left(psx + 6.0f);

    set_demo_select(true);                                          // show a sliding target cursor in the preview
    for (int t = 0; t < 3; ++t) if (tiers[t] && (t == 0 ? C.partyShow : C.allyShow)) tiers[t]->draw(f);   // tier 0 first (publishes the stack ref) ; honour per-side Show
    set_demo_select(false);
    set_party_preview_buff_left(0.0f);                              // back to live : no strip cap / "+N" on the real HUD

    C.box[0].posSet = sp0; C.box[1].posSet = sp1; C.box[2].posSet = sp2;
    for (int i = 0; i < 6; ++i) C.partyRef[i] = sref[i];
    C.partyBottomY = sbot;
    for (int i = 0; i < 4; ++i) C.allyRefY[i] = sally[i];
    for (int t = 0; t < 3; ++t) if (tiers[t]) tiers[t]->set_origin(opx[t], opy[t]);   // restore real placement
    set_party_demo_level(savedLvl); set_party_demo_count(savedCnt);
}

} // namespace aio
