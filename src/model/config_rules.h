// config_rules.h -- the two decisions the config layer makes, lifted out of the file I/O.
//
// Extracted 2026-09-12 (element 5 of docs/audits/plan-harnais-2026-09-09.md). ui_config.cpp is ~1400 lines of
// hand-written serialisation over ~130 keys and ~300 persisted fields, maintained in FOUR places (writer,
// reader, persist_eq, reset). Tests already existed for the round-trip -- and the plan's own warning about
// this line was that tests which do not encode the INCIDENTS prove very little. The incidents:
//
//   - `mm3=` collided with the clock's `mm3=` and never loaded : a whole block of minimap settings that saved
//     and came back as the default. Covered by the round-trip test (t_config.cpp).
//   - `mmZoom` reached a sprintf into char[16] from a hand-edited file and killed the client. One field was
//     clamped, 85 others were not -- that is what `config_sanitise` is.
//   - `scTP` was the ONE field of 302 that "Reset all settings" forgot, because the reset was a hand-written
//     list of 300 assignments. That is what `config_defaults` is : the list becomes structural.
//   - a profile load OVERLAID the live config instead of replacing it, so a key the file does not carry kept
//     the PREVIOUS profile's value -- and the next save wrote that value into the file. Measured on the
//     shipped asset: assets/default_profile.txt carries 106 keys where the writer emits 130, so loading
//     "Default" silently kept the current partyShow / allyShow / iconpack / Debuffs / zone-tracker rows, then
//     re-saved them into Default. `config_defaults` is the other half of that fix : a config FILE is a
//     complete config, and what it does not say is the default, not "whatever was there before".
//
// PURE, in the sense the plan requires : no file, no device, no game memory, no clock. It takes the struct and
// returns nothing but a changed struct, so every case below can be written as a value and checked offline.
#pragma once
#include "model/ui_config.h"

namespace aio {

// ---- 1. what a LOAD or a RESET starts from -------------------------------------------------------------
// Everything goes back to the code defaults EXCEPT the three things that are not the file's to decide:
//
//   - `lang` : the UI language. A profile written before the setting existed carries no lang= line, and
//     resetting it would leave the user reading a UI they may not understand, with the control to fix it in
//     that same language. reset_ui_config() has refused to reset it for the same reason since it was written.
//   - `favColors` : the personal swatch palette, shared by every colour picker. It is written into each
//     profile file but it is NOT part of persist_eq -- i.e. the project already treats it as global, not as
//     something a profile owns. Wiping it on a load would throw away work no profile claims.
//   - `selfTest` : whether the in-game watchers are armed. A session/machine flag, not an appearance a
//     profile owns -- `//aio selftest on` must survive a relaunch, which is the whole promise written at its
//     writer ("a harness you must re-arm is one you forget to arm"). It is deliberately NOT added to
//     persist_eq instead: that would make the profile show "unsaved changes" every time a watcher is armed.
//     (Both holes found by the config audit of 2026-09-12 -- the favColors test, applied to its peers.)
//   - the edit-mode zones, when `keepZones` : "Reset all settings" has never cleared them (measured -- they
//     are the only persisted field the old hand-written reset left standing on purpose), because they are a
//     layout the user DREW and their own Default button lives in edit mode. A profile load does clear them:
//     zones are written to profile files as `zone=` lines and have always been rebuilt from the file.
inline void config_defaults(UiConfig& c, bool keepZones) {
    static const UiConfig DEF{};   // value-initialised ({} matters : tmBuffOff / guideGroup have
                                   // no in-class initialiser, so a plain `UiConfig d;` would copy garbage)
    const int lang = c.lang;
    const int selfTest = c.selfTest;
    const int favN = (c.favColorN < 0) ? 0 : (c.favColorN > UiConfig::FAV_COLOR_MAX ? UiConfig::FAV_COLOR_MAX : c.favColorN);
    unsigned fav[UiConfig::FAV_COLOR_MAX];
    for (int i = 0; i < favN; ++i) fav[i] = c.favColors[i];
    int zn = keepZones ? c.guideGroupCount : 0;
    if (zn < 0) zn = 0; else if (zn > GUIDE_GROUPS_MAX) zn = GUIDE_GROUPS_MAX;
    GuideGroup zones[GUIDE_GROUPS_MAX];   // ~2 KB : this runs on a load or a reset, never per frame
    for (int i = 0; i < zn; ++i) zones[i] = c.guideGroup[i];

    c = DEF;

    c.lang = lang;
    c.selfTest = selfTest;
    c.favColorN = favN;
    for (int i = 0; i < favN; ++i) c.favColors[i] = fav[i];
    c.guideGroupCount = zn;
    for (int i = 0; i < zn; ++i) c.guideGroup[i] = zones[i];
}

// ---- 2. which values are admissible -------------------------------------------------------------------
// A hand-edited file, a file from a future build, a half-written one from a crashed client : none of them may
// feed a draw loop a multiplier of 1e30, a zero scale, or a widget position off the screen where the user
// cannot grab it back. The config UI enforces its own ranges already; this is the same defence at the SOURCE,
// so an absurd value is not merely survived at one draw site but never re-serialised by the next save.
//
// THE BOUNDS ARE DELIBERATELY WIDER THAN THE SLIDERS. A loader must not MOVE a value that some past build
// allowed: the Size slider is 0.50..2.00 today, so clamping a stored 0.45 to 0.50 would silently edit the
// user's config. These bounds are the ones no UI has ever been able to exceed -- they kill 0, negatives,
// 1e30 and NaN, and touch nothing a human ever set. Positions are the exception and are clamped to the
// screen fraction [0,1] exactly, because edit_box.cpp ALREADY clamps every drag to [0,1] and keeps the box
// fully on screen (edit_box.cpp:167-185) -- so no reachable layout lies outside it.
static const float CFG_MUL_LO = 0.10f, CFG_MUL_HI = 4.0f;   // "size multiplier" fields (sliders live in 0.5..3.0)
static const float CFG_GAP_HI = 4.0f;                       // spacing / outline fields, where 0 is a legal "touching"

// NaN is not "out of range" to `<` and `>` : both comparisons are FALSE, so the classic min/max clamp lets it
// straight through, and sscanf("%f") does parse "nan" and "inf" out of a hand-edited file. It goes back to the
// field's DEFAULT rather than to the floor, because a floor is the wrong answer for half these fields -- an
// alpha of 0 is an invisible box, which reads as a bug, not as a repaired value.
inline float cfg_num(float v, float lo, float hi, float def) {
    if (v != v) return def;
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int cfg_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Typography. `face` is deliberately NOT clamped : ui_font_face() already answers "" for an out-of-range
// index and GDI falls back to a system face, so it degrades on its own -- and the count lives in ui_config.cpp,
// which this header must not need.
inline void cfg_text(TextStyle* t, int n) {
    static const TextStyle D{};
    for (int i = 0; i < n; ++i) {
        t[i].size    = cfg_num(t[i].size,    CFG_MUL_LO, CFG_MUL_HI, D.size);      // 0 = invisible text, and no reset explains it
        t[i].outline = cfg_num(t[i].outline, 0.0f,       CFG_GAP_HI, D.outline);   // 0 = no outline is legal
    }
}
inline void cfg_box(BoxStyle& b) {
    static const BoxStyle D{};
    b.alpha = cfg_num(b.alpha, 0.0f, 1.0f, D.alpha);
    b.lum   = cfg_num(b.lum,  -1.0f, 1.0f, D.lum);
}

inline void config_sanitise(UiConfig& c) {
    static const UiConfig D{};
    #define MUL(x)   c.x = cfg_num(c.x, CFG_MUL_LO, CFG_MUL_HI, D.x)
    #define GAP(x)   c.x = cfg_num(c.x, 0.0f, CFG_GAP_HI, D.x)
    #define FRAC(x)  c.x = cfg_num(c.x, 0.0f, 1.0f, D.x)        // screen fraction : see edit_box.cpp
    #define ALPHA(x) c.x = cfg_num(c.x, 0.0f, 1.0f, D.x)
    #define LUM(x)   c.x = cfg_num(c.x, -1.0f, 1.0f, D.x)

    // ---- party / alliance strip + shared chrome
    MUL(buffScale); MUL(cursorScale);
    LUM(skinLum); ALPHA(skinBoxAlpha); LUM(allyLum); ALPHA(allyBoxAlpha);
    c.buffMax  = cfg_int(c.buffMax, 0, 32);   // 0 = no party/alliance buffs at all : a state the UI offers
    c.buffRows = cfg_int(c.buffRows, 1, 2);
    c.uiStyle  = cfg_int(c.uiStyle, 0, 15);
    c.uiColor  = cfg_int(c.uiColor, 0, 35);   // 12 hues x 3 lightness rows
    for (int k = 0; k < 3; ++k) {
        MUL(barHeight[k]); MUL(barWidth[k]); MUL(badgeScale[k]);
        // box[] keeps its own tighter pair : x/y are clamped at the parse site with the same argument, and
        // 0.50..2.0 is what the edit-mode wheel and the Size slider enforce.
        c.box[k].x = cfg_num(c.box[k].x, 0.0f, 1.0f, 0.0f);
        c.box[k].y = cfg_num(c.box[k].y, 0.0f, 1.0f, 0.0f);
        c.box[k].scale = cfg_num(c.box[k].scale, 0.50f, 2.0f, 1.0f);
        c.gaugeStyle[k] = (c.gaugeStyle[k] < 0 || c.gaugeStyle[k] > 7) ? 0 : c.gaugeStyle[k];
        c.jobBadge[k]   = (c.jobBadge[k]   < 0 || c.jobBadge[k]   > 3) ? 2 : c.jobBadge[k];
    }
    cfg_text(c.text[0], TE_COUNT); cfg_text(c.text[1], TE_COUNT);

    // ---- Target
    ALPHA(tgtBoxAlpha); LUM(tgtLum);
    MUL(tgtScale); MUL(tgtBarH); MUL(tgtBarW); MUL(tgtIconSz); MUL(tgtDetailIconSz); MUL(tgtRangeH);
    FRAC(tgtX); FRAC(tgtY);
    c.tgtBuffMax = cfg_int(c.tgtBuffMax, 0, 20);
    cfg_text(c.tgtText, TGT_TE_COUNT);

    // ---- Player hub + equipment
    ALPHA(plrBoxAlpha); LUM(plrLum);
    MUL(plrScale); MUL(plrEqCell); MUL(plrEquipScale); MUL(plrBarH); MUL(plrBarW); MUL(plrIconSz); MUL(plrEmblemSz);
    GAP(plrBarGap);   // 0 = the fioles touch
    FRAC(plrX); FRAC(plrY); FRAC(plrEquipX); FRAC(plrEquipY);
    c.plrBuffMax = cfg_int(c.plrBuffMax, 0, 32);
    cfg_text(c.plrText, PLR_TE_COUNT);
    cfg_box(c.plrEqBox);

    // ---- Minimap (+ its clock box)
    MUL(mmScale); MUL(mmMapSize); MUL(mmBezelW); MUL(mmCardSz); MUL(mmSqBorder); MUL(mmMarkerScale);
    ALPHA(mmBgAlpha);
    FRAC(mmX); FRAC(mmY);
    // mmZoom was the 2026-07-26 S0 : 1e30 from a hand-edited file printed 35 bytes into a char[16].
    c.mmZoom  = cfg_num(c.mmZoom, 1.0f, 24.0f, D.mmZoom);
    c.mmRingR = cfg_num(c.mmRingR, 0.0f, 50.0f, D.mmRingR);   // yalms ; the slider is 3..50
    cfg_text(c.mmText, MM_TE_COUNT);
    cfg_box(c.mmBox);

    // ---- the standalone widgets
    MUL(wsScale);   FRAC(wsX);   FRAC(wsY);
    MUL(scScale);   FRAC(scX);   FRAC(scY);   GAP(scListGap);   cfg_text(c.scText, SC_TE_COUNT);   cfg_box(c.scBox);
    MUL(tpScale);   FRAC(tpX);   FRAC(tpY);                     cfg_text(c.tpText, TP_TE_COUNT);   cfg_box(c.tpBox);
    MUL(hlScale);   FRAC(hlX);   FRAC(hlY);                     cfg_text(c.hlText, HL_TE_COUNT);   cfg_box(c.hlBox);
    MUL(pwScale);   FRAC(pwX);   FRAC(pwY);                     cfg_text(c.pwText, PW_TE_COUNT);   cfg_box(c.pwBox);
    MUL(grimScale); FRAC(grimX); FRAC(grimY);                   cfg_text(c.grimText, GRIM_TE_COUNT);
    MUL(epScale);   FRAC(epX);   FRAC(epY);                     cfg_text(c.epText, EP_TE_COUNT);   cfg_box(c.epBox);
    c.tpCount = cfg_int(c.tpCount, 0, 10);
    c.hlCount = cfg_int(c.hlCount, 0, 20);

    // ---- Zone tracker (one bar/dot factor per zone family)
    MUL(ztScale); FRAC(ztX); FRAC(ztY);
    MUL(ztDyBarW); MUL(ztDyBarH); MUL(ztDyDot);
    MUL(ztAbBarW); MUL(ztAbBarH); MUL(ztAbLightW); MUL(ztAbLightH);
    MUL(ztShIcon); MUL(ztShDot); MUL(ztLbBarW); MUL(ztLbBarH);
    cfg_text(c.ztText, ZT_TE_COUNT); cfg_box(c.ztBox);

    // ---- Timers / Debuffs
    MUL(tmScale); FRAC(tmX); FRAC(tmY); FRAC(tmRX); FRAC(tmRY); MUL(tmIconScale); GAP(tmRowGap);
    c.tmMax = cfg_int(c.tmMax, 0, 50);
    c.tmFocusWarn = cfg_int(c.tmFocusWarn, 10, 300);
    c.tmFocusHold = cfg_int(c.tmFocusHold, 5, 300);
    cfg_text(c.tmText, TM_TE_COUNT); cfg_box(c.tmBox);
    MUL(dbScale); FRAC(dbX); FRAC(dbY); MUL(dbIconScale); GAP(dbRowGap);
    c.dbMax = cfg_int(c.dbMax, 0, 32);
    cfg_text(c.dbText, DB_TE_COUNT); cfg_box(c.dbBox);

    // NOT clamped, and each for a reason:
    //   - partyRef[] / partyRefX[] / allyRefY[] / partyBottomY / zonePanelX / zonePanelY use -1 as "unset",
    //     so a [0,1] clamp would turn every unset reference line into a real one at the screen edge.
    //   - the mode / theme / variant integers (~60 of them) are guarded at their draw sites and each needs
    //     its own panel read to bound honestly. Out of scope here, on purpose, rather than guessed at.
    #undef MUL
    #undef GAP
    #undef FRAC
    #undef ALPHA
    #undef LUM
}

} // namespace aio
