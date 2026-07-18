// ui_config.h -- live, user-editable HUD settings (edited by the //aio config page, read by the
// widgets + Hud each frame). A process-wide singleton, like party(). Start small (the Party/Alliance
// basics) ; grow as more controls are added. Not yet persisted to disk.
#pragma once

namespace aio {

// One movable HUD block. x/y = top-left in SCREEN FRACTION (0..1) when posSet ; else the block uses
// its default placement (party = layout anchor ; alliances = stacked above the party).
struct BoxLayout {
    bool  posSet = false;
    float x = 0.0f, y = 0.0f;
    float scale = 1.0f;        // size multiplier (font + geometry)
};

// ---- user-drawn ZONES (simplified model). A zone is a named RECTANGLE you draw by drag-and-drop in edit
// mode's "Rules: ON" screen (rubber-band), then move / resize / rename. Permissions (allow[ZPERM_*]) say which
// HUD box may sit on it -- a box is pushed OUT of any zone that forbids its type (all-false = fully forbidden).
// The party-SIZING reference lines (partyRef / allyRefY below) are a SEPARATE system, kept as-is. ----
enum { ZPERM_PARTY = 0, ZPERM_ALLIANCE, ZPERM_HUB, ZPERM_TARGET, ZPERM_COUNT };
struct GuideGroup {
    float x = 0.40f, y = 0.42f, w = 0.20f, h = 0.16f;   // top-left + size, screen fractions
    char  name[20] = { 0 };
    bool  allow[ZPERM_COUNT] = { false, false, false, false };
    int   role = 0;   // 0 = normal zone ; 1..6 = PARTY box for that member count (TOP sizes it) ; 7 = ALLIANCE 1, 8 = ALLIANCE 2
};
static const int GUIDE_GROUPS_MAX = 48;

// top Y (pixels) of the party-role zone for `count` members (its rectangle top drives the party box grow-up),
// or -1 if there is no such zone (caller then falls back to partyRef). See party.cpp.
float guide_party_top(int count, float sh);
// fill ar[0..3] = {A1 top, A1 bottom, A2 top, A2 bottom} (screen fractions) from the alliance-role zones
// (role 7 = A1, role 8 = A2). Returns true only if BOTH exist ; else the caller uses allyRefY. See party.cpp.
bool  guide_alliance_refs(float* ar);

// ---- per-element typography (global : applies to every box) ----
enum { TE_NAME = 0, TE_HP, TE_MP, TE_TP, TE_CAST, TE_BADGE, TE_DIST, TE_UI, TE_COST, TE_COUNT };
enum { TGT_NAME = 0, TGT_HP, TGT_TIMER, TGT_SPD, TGT_TH, TGT_RANGE, TGT_TE_COUNT };   // Target box text elements (name / HP% / debuff timer / speed% / TH level / distance labels+number)
enum { PLR_NAME = 0, PLR_LVL, PLR_GIL, PLR_SPEED, PLR_HP, PLR_MP, PLR_TP, PLR_CAST, PLR_TE_COUNT };   // Player Hub text elements (name / job-level / gil / speed / HP-MP-TP bar values / cast-spell name ; the emblem is an icon, sized separately)
enum { MM_TIME = 0, MM_DAY, MM_MOON, MM_REAL, MM_TE_COUNT };   // Minimap clock text elements (Vana'diel time / elemental day / moon %+New-Full / real-GMT)
enum { SC_TITLE = 0, SC_TIMER, SC_STEP, SC_PROP, SC_LIST, SC_TE_COUNT };   // Skillchains text elements (Title / Timer / Step / Property / WS list)
enum { TP_IDX = 0, TP_NAME, TP_TIMER, TP_LOOT, TP_TE_COUNT };   // Treasure Pool text elements (Index / Name / Timer / Lotter)
enum { HL_DIST = 0, HL_NAME, HL_PCT, HL_TARGET, HL_TE_COUNT };  // Hate List text elements (Distance / Name / HP% / Target)
enum { PW_LABEL = 0, PW_VALUE, PW_RATE, PW_TE_COUNT };          // PointWatch text elements (Label / Value / Rate)
enum { GRIM_CHARGE = 0, GRIM_TIMER, GRIM_TE_COUNT };           // Grimoire text elements (Charge count / Recast timer)
enum { ZT_HEADER = 0, ZT_BODY, ZT_TE_COUNT };                  // Zone Tracker text elements (Header / Body)
enum { EP_TITLE = 0, EP_POP, EP_FROM, EP_COLL, EP_TE_COUNT };  // EmpyPop text elements (NM title / pop name / "from <mob>" / collectable)
enum { TM_HEADER = 0, TM_NAME, TM_TIMER, TM_TE_COUNT };        // Timers text elements (column titles / buff+recast name / MM:SS)
enum { DB_HEADER = 0, DB_NAME, DB_TIMER, DB_TE_COUNT };        // Debuffs module text elements (mob-name header / debuff name / timer)
enum { TMDISP_ICON = 0, TMDISP_NAME, TMDISP_BOTH };           // Timers column display : Icon / Name / Icon+Name
enum { TMSRC_MINE = 0, TMSRC_PLAYERS, TMSRC_TRUSTS, TMSRC_ALL };   // Timers Duration buff-source filter : mine / +players / +trusts / all
struct TextStyle {
    int   face    = 0;     // 0 = default (layout / global face) ; else index into the ui_font list
    float size    = 1.0f;  // multiplier on the element's base size
    float outline = 1.0f;  // multiplier on the element's outline width
    bool  bold    = false, italic = false, upper = false, colorOn = false;
    unsigned color = 0xFFFFFFFFu;   // tint used when colorOn (else the element keeps its own semantic colour)
};
const char* ui_text_elem_label(int e);   // "Name" / "HP" / ... / "Interface"

// The persisted "box appearance" bundle every non-Party module uses (Target/Player already inline the same fields).
// Defaults = follow the Party theme, solid. See ui/box_style.{h,cpp} for the shared draw + config-row helpers.
struct BoxStyle {
    int      on        = 1;      // draw the box chrome at all (0 = the content floats bare : no bg, no border)
    int      border    = 1;      // draw the FRAME edges + corners (0 = background only -> a borderless panel). Needs on.
    float    alpha     = 1.0f;   // chrome opacity (1 = solid ; user "Transparency" slider = 1 - alpha)
    int      themeCopy = 1;      // 1 = follow the Party box theme (skinTheme/skinLum/skinHue) ; 0 = this box's own
    int      theme     = 0;      // own theme index (window_theme_index) when themeCopy = 0
    float    lum       = 0.0f;   // own luminosity  (-1 darker .. +1 lighter)
    unsigned hue       = 0;      // own custom hue  (0 = the theme's preset colour)
};

struct UiConfig {
    // ---- Party / Alliance ----
    TextStyle text[2][TE_COUNT];   // per-element typography, per config group : [0]=Party, [1]=Alliance (Interface font = [0][TE_UI])
    int   partyShow = 1;       // master on/off : draw the main PARTY box (0 = hide the whole party module)
    int   skinTheme = 0;       // window-skin theme index (the Hud applies it -> all boxes)
    float skinLum   = 0.0f;    // box-theme luminosity : -1 = darker .. 0 = neutral .. +1 = lighter (procedural themes)
    float skinBoxAlpha = 1.0f; // PARTY/ALLIANCE box chrome OPACITY (1 = solid .. 0 = fully see-through) ; content stays opaque
    unsigned skinHue = 0;      // custom box-theme HUE (0 = use the theme's preset ; else this opaque colour tints the family)
    // ---- Alliance boxes : optionally their OWN theme (else follow the Party skinTheme). Same encoding as skinTheme. ----
    int   allyShow = 1;        // master on/off : draw the two ALLIANCE boxes (0 = hide alliance ; party can still show)
    int   allyThemeCopy = 1;   // 1 = alliance boxes follow the Party box theme ; 0 = their own theme below
    int   allyTheme    = 0;    // alliance box-theme index (family x hue, like skinTheme)
    float allyLum      = 0.0f; // alliance box-theme luminosity (procedural themes)
    float allyBoxAlpha = 1.0f; // alliance box chrome opacity (1 = solid .. 0 = see-through)
    unsigned allyHue   = 0;    // alliance custom box-theme hue (0 = theme preset)
    int   fontFace  = 0;       // 0 = layout default ; >0 = override every party/alliance text face
    float buffScale = 0.92f;   // buff-icon size as a FRACTION of the member row height (0.40 .. 1.00, capped at the row)
    int   buffMax   = 20;      // max buff icons shown per member (config choice) ; > 16 wraps to TWO rows of 16
    int   buffRows  = 2;       // buff strip : 1 or 2 rows ; 1 = one bigger (full-line-tall) row
    int   uiStyle   = 0;       // config-menu colour STYLE / family (Neon / Matte / Medieval / Heroic / Pastel ...)
    int   uiColor   = 0;       // colour index WITHIN the chosen style
    unsigned uiAccent = 0;     // custom config-menu accent (0 = use uiStyle/uiColor preset ; else derive the accent family from this opaque colour)
    int   uiCursor  = 0;       // draw AioHud's OWN mouse cursor in the config window (for players whose modded game DAT hides the native cursor) ; 0 = rely on the game cursor
    int   hidePeekMode = 0;    // the HUD-hide key (End) : 0 = HOLD (hidden only while held) ; 1 = TOGGLE (press to hide, press again to show)
    float cursorScale = 1.0f;  // selection-cursor (hand) size multiplier (0.50 .. 2.00)
    // ---- Target module (its OWN box theme, independent of the party skinTheme) ----
    int   tgtShow    = 1;      // master on/off : draw the Target module at all (0 = never show the target box)
    int   tgtBox     = 1;      // 1 = draw the box chrome ; 0 = NO box (name/%/bar/buffs/timer float with a heavy outline)
    float tgtBoxAlpha = 1.0f;  // box chrome OPACITY (1 = solid .. 0.1 = near-transparent) ; the content stays opaque
    int   tgtThemeCopy = 0;    // 1 = the Target box FOLLOWS the party box theme (skinTheme/skinLum) ; 0 = its own below
    int   tgtTheme   = 0;      // Target box-theme index (family x hue, same encoding as skinTheme)
    float tgtLum     = 0.0f;   // Target box-theme luminosity (-1 darker .. +1 lighter ; procedural themes)
    unsigned tgtHue  = 0;      // Target custom box-theme HUE (0 = preset ; else opaque custom colour)
    float tgtScale   = 1.0f;   // Target box size multiplier (0.50 .. 2.00), on top of the layout scale
    int   tgtNameHostile = 1;  // colour the target NAME by hostility : red = claimed by another, orange = engaged/in combat
    int   tgtSpeed   = 1;      // show the target's movement SPEED % (green = slower, red = faster) in a detail row
    int   tgtSpeedIcon = 0;    // Speed display : 0 = text ("Spd +12%") / 1 = buff-atlas icon (cell 32) + value
    int   tgtTH      = 1;      // show the Treasure Hunter level applied to the target
    int   tgtThIcon  = 0;      // TH display : 0 = text ("TH9") / 1 = coffer icon + value
    int   tgtRange   = 1;      // show the DISTANCE gauge (range zones : Melee/WS/Magic/... on a mob, Trade/AoE/Cast on an ally)
    float tgtRangeH  = 1.0f;   // DISTANCE gauge height multiplier (like tgtBarH for the HP bar)
    int   tgtRangeMin = 0;     // Distance display : 0 = full gauge (zones + cursor) ; 1 = MINIMAL (just the number,
                               // coloured by the RANGE ZONE it falls in -- same band colours as the gauge : Melee/WS/Magic/Ranged/Enmity on a mob, Trade/AoE/Cast on an ally)
    int   tgtCast    = 1;      // show the target's ACTION bar (its live spell cast : name + filling bar) under the HP bar
    int   tgtCastDemo = 0;     // show a DEMO cast bar during normal play (placeholder for positioning ; live cast wins)
    int   tgtSub     = 1;      // show the SUB-TARGET (<st>) compact bar (name + mini HP) at the bottom while a sub-target cursor is up
    int   tgtSubPos  = 0;      // SUB-TARGET box placement relative to the main box : 0 Above-Right 1 Above-Left 2 Below-Right 3 Below-Left 4 Right 5 Left
    int   tgtDebuffs = 1;      // show the debuff icons row on the target box (0 = off)
    int   tgtBuffMax = 20;     // max debuff icons shown (1..20 ; the atlas layout wraps 10 per row/column)
    int   tgtBuffPos = 0;      // debuff row placement : 0 = inside (box grows) / 1 = below / 2 = above / 3 = left / 4 = right (1-4 = OUTSIDE the box)
    int   tgtTimers  = 1;      // show the countdown timer under each debuff icon (0 = icons only)
    TextStyle tgtText[TGT_TE_COUNT];   // per-element typography : [TGT_NAME] name, [TGT_HP] HP% number, [TGT_TIMER] debuff timer
    float tgtBarH    = 1.0f;   // HP bar HEIGHT multiplier
    float tgtBarW    = 1.0f;   // HP bar WIDTH as a fraction of the inner width (0.50 .. 1.00), centred
    float tgtIconSz  = 1.0f;   // debuff icon size multiplier
    float tgtDetailIconSz = 1.6f;   // Speed/TH detail-row ICON size multiplier (icon display mode only)
    bool  tgtPosSet  = false;  // Target box has a user-placed position (edit mode) ; else the layout.json default
    float tgtX = 0.0f, tgtY = 0.0f;   // top-left as a FRACTION of the screen when tgtPosSet
    int   tgtCenterH = 0, tgtCenterV = 0;   // LOCK the box centred on the screen H / V axis (widget re-centres each frame ; a drag releases it)
    // ---- Player Hub module (single box, like Target) : Phase 1 = identity + vitals + buffs ----
    int   plrShow    = 1;      // master on/off : draw the Player Hub at all (0 = never show the player box)
    int   plrBox     = 1;      // 1 = draw the box chrome ; 0 = no box
    float plrBoxAlpha = 1.0f;  // box chrome OPACITY (content stays opaque)
    int   plrThemeCopy = 1;    // 1 = the Player box FOLLOWS the party box theme (skinTheme/skinLum) ; 0 = its own below
    int   plrTheme   = 0;      // Player box-theme index (family x hue, same encoding as skinTheme)
    float plrLum     = 0.0f;   // Player box-theme luminosity (-1 darker .. +1 lighter ; procedural themes)
    unsigned plrHue  = 0;      // Player custom box-theme HUE (0 = preset ; else opaque custom colour)
    bool  plrPosSet  = false;  // Player Hub has a user-placed position (edit mode) ; else the layout.json default
    float plrX = 0.0f, plrY = 0.0f;   // top-left as a FRACTION of the screen when plrPosSet
    int   plrCenterH = 0, plrCenterV = 0;   // LOCK the box centred on the screen H / V axis (drag-to-centre snaps it)
    float plrScale   = 1.0f;   // Player Hub size multiplier (0.50 .. 2.00), on top of the layout scale
    int   plrEmblem  = 1;      // show the job emblem (independent on/off)
    int   plrName    = 1;      // show the player name (independent on/off)
    int   plrLvl     = 1;      // show the main/sub job & levels line (independent on/off)
    int   plrHp      = 1, plrMp = 1, plrTp = 1;   // show each vitals fiole
    int   plrGil     = 1;      // show the gil band (coin icon + amount)
    int   plrSpeed   = 1;      // show the movement-speed band (Speed icon + %)
    int   plrCast    = 1;      // show the player's ACTION bar (own Magic cast / job ability / WS : name + filling bar)
    int   plrCastDemo = 0;     // show a DEMO cast bar during normal play (placeholder for positioning ; live cast wins)
    int   plrEquip   = 1;      // show the equipment viewer (4x4 grid of the 16 equipped items)
    float plrEqCell  = 1.0f;   // equipment cell size multiplier (0.50 .. 2.00)
    int   plrEqThemeBorder = 1;// 1 = cell borders follow the box theme (proc hue / FFXI skin) ; 0 = custom colour below
    unsigned plrEqColor = 0xFF6699BBu;   // custom cell-border colour (used when plrEqThemeBorder == 0)
    int   plrEqCellBgCustom = 0;         // 1 = custom cell BACKGROUND colour (plrEqCellBg) ; 0 = the default dark
    unsigned plrEqCellBg = 0xE0121620u;  // custom equipment cell background (occupied top ; empty + bottom are derived shades)
    int   plrEqPlace = 0;      // equipment grid placement : 0 = in-box centre, 1 = in-box left, 2 = in-box right, 3 = docked LEFT (outside), 4 = docked RIGHT (outside)
    int   plrEquipDetach = 0;  // 0 = equipment lives INSIDE the Player Hub ; 1 = STANDALONE module (own position + size below, dragged in //aio edit)
    bool  plrEquipPosSet = false;  // standalone equipment has a user-placed position (edit mode) ; else a default spot
    float plrEquipX = 0.0f, plrEquipY = 0.0f;   // standalone grid top-left as a FRACTION of the screen when plrEquipPosSet
    float plrEquipScale = 1.0f;   // standalone equipment size multiplier (0.50 .. 2.00), independent of the Hub scale
    int   plrEqGilPlace = 0;   // STANDALONE gil position relative to the grid : 0 below, 1 above, 2 left, 3 right (docked always draws it below)

    // --- Minimap (phase 1) ---
    int   mmShow = 1;          // show the minimap
    int   mmClock = 1;         // Vana'diel clock header (time / elemental day / moon / real time) above the map
    int   mmClkTime = 1;       // clock : show the Vana'diel time
    int   mmClkDay  = 1;       // clock : show the elemental day (-> next)
    int   mmClkMoon = 1;       // clock : show the moon phase
    int   mmClkReal = 1;       // clock : show the real / GMT time row
    int   mmClockPos = 0;      // clock header placement vs the map : 0 Top, 1 Bottom, 2 Left, 3 Right
    float mmMapSize = 1.0f;    // map diameter multiplier, INDEPENDENT of the box/header scale (0.5 .. 1.6)
    float mmBezelW  = 1.0f;    // ROUND : brass/copper bezel ring WIDTH multiplier (0.5 .. 2.0)
    float mmCardSz  = 1.0f;    // ROUND : N/S/E/W cardinal-letter SIZE multiplier (0.5 .. 2.0)
    int   mmBezel   = 1;       // ROUND : 1 = draw the brass bezel + cardinals ; 0 = none (just the round map lens)
    float mmSqBorder = 1.0f;   // SQUARE : frame/border THICKNESS multiplier (0.5 .. 2.0)

    // --- Arcade "ULTRA COMBO" weaponskill popup ---
    int   wsShow    = 1;       // show the centre-screen popup on YOUR weaponskill
    float wsScale   = 1.0f;    // overall size multiplier (0.5 .. 2.5)
    float wsX       = 0.5f;    // horizontal centre (fraction of the screen)
    float wsY       = 0.36f;   // vertical centre (fraction of the screen)
    int   wsFont    = 0;       // font face index (ui_font_face ; the popup always renders heavy/bold)
    int   wsFx      = 1;       // impact effects (flash + shockwave ring + pulsing glow)
    unsigned wsNameCol = 0xFFFFA518u;   // WS-name colour
    unsigned wsDmgCol1 = 0xFFFFF024u;   // damage colour A (the flash cycles A<->B)
    unsigned wsDmgCol2 = 0xFFFF5A0Au;   // damage colour B
    // ---- Skillchains module : a box on your target's active skillchain (step / property / burst window) ----
    int   scShow  = 1;         // show the skillchains box
    float scScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float scX     = 0.78f;     // horizontal CENTRE (screen fraction) -> box grows symmetrically ; scY = its TOP
    float scY     = 0.06f;
    int   scTitle = 1;         // show the "Skillchains" title line
    int   scTimer = 1;         // show the Wait/Go!/Burst timer line
    int   scStep  = 1;         // show the "Step: N > <move>" line
    int   scProps = 1;         // show the "[property] (elements)" line
    int   scList  = 1;         // show the continuation WS list
    float scListGap = 1.0f;    // vertical spacing multiplier between each WS in the continuation list (0.6 .. 3.0)
    int   scNearby = 1;        // display scope : 1 = beyond your cursor target <t>, ALSO show your battle-target <bt>
                               //   (engaged, reticle off) then the newest LIVE chain on a nearby mob (party members'
                               //   SCs) ; 0 = STRICT, only your current cursor target <t>'s chain (nothing while you
                               //   target nothing / an ally, even if engaged)
    TextStyle scText[SC_TE_COUNT];   // per-element typography : [SC_TITLE] [SC_TIMER] [SC_STEP] [SC_PROP] [SC_LIST]
    // ---- Treasure Pool module : the lottery pool (coffer icon + idx / name / timer / lotter columns) ----
    int   tpShow  = 1;         // show the treasure-pool box
    float tpScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float tpX     = 0.72f;     // horizontal CENTRE (screen fraction) ; tpY = its TOP
    float tpY     = 0.30f;
    int   tpCount = 10;        // max items shown (1..10)
    int   tpIcon  = 1;         // show the coffer icon
    TextStyle tpText[TP_TE_COUNT];   // per-element typography : [TP_IDX] [TP_NAME] [TP_TIMER] [TP_LOOT]

    int   hlShow  = 1;         // show the hate-list box (mobs aggro'd on you / your party)
    float hlScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float hlX     = 0.22f;     // horizontal CENTRE (screen fraction) ; hlY = its TOP
    float hlY     = 0.32f;
    int   hlCount = 8;         // max mobs shown (1..20)
    int   hlDist  = 1;         // show the Distance column (left)
    int   hlTgt   = 1;         // show the Target column (who each mob is on, right)
    TextStyle hlText[HL_TE_COUNT];   // per-element typography : [HL_DIST] [HL_NAME] [HL_PCT] [HL_TARGET]

    int   pwShow  = 1;         // show the PointWatch box (XP / CP / ML + Merits)
    float pwScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float pwX     = 0.015f;    // horizontal CENTRE (screen fraction) ; pwY = its TOP
    float pwY     = 0.04f;
    int   pwMode  = 0;         // progression stage : 0 = Auto (by level/ML), 1 = XP, 2 = CP, 3 = ML
    int   pwLayout = 0;        // content layout : 0 = vertical (rows stacked) ; 1 = horizontal (side by side)
    int   pwDisplay = 0;       // what to show per stat : 0 = both (text + bar) ; 1 = text only ; 2 = bar only
    int   pwRate  = 1;         // show the per-hour rate (X/h)
    TextStyle pwText[PW_TE_COUNT];   // per-element typography : [PW_LABEL] [PW_VALUE] [PW_RATE]

    int   grimShow  = 1;       // show the Scholar grimoire (only appears for a SCH main/sub)
    float grimScale = 1.0f;    // size multiplier (0.5 .. 2.0)
    float grimX     = 0.28f;   // horizontal CENTRE (screen fraction) ; grimY = its TOP
    float grimY     = 0.58f;
    int   grimArt   = 2;       // EDIT-preview art : 0 Light, 1 Dark, 2 Addendum White, 3 Addendum Black
    TextStyle grimText[GRIM_TE_COUNT];   // per-element typography : [GRIM_CHARGE] [GRIM_TIMER]

    int   ztShow    = 1;       // show the Zone Tracker (only appears in a Dynamis / Abyssea zone)
    float ztScale   = 1.0f;    // size multiplier (0.5 .. 2.0)
    float ztX       = 0.145f;  // horizontal CENTRE (screen fraction) ; ztY = its TOP
    float ztY       = 0.33f;
    int   ztVariant = 1;       // EDIT-preview zone : 0 Dynamis, 1 Abyssea, 2 Omen, 3 Nyzul, 4 Sheol
    int   ztHeader  = 1;       // show the box TITLE row (Dynamis / Abyssea / Omen / Nyzul Isle / Sheol X)
    int   ztSheolSeg  = 1;     // Sheol : show the segment counter
    int   ztSheolRes  = 1;     // Sheol : show the target's resistances (weapons / elements)
    int   ztSheolJoke = 1;     // Sheol : show the Cruel Joke vulnerability line
    TextStyle ztText[ZT_TE_COUNT];   // per-element typography : [ZT_HEADER] [ZT_BODY]
    // ---- EmpyPop module : the pop items / key items needed to spawn an Abyssea empyrean NM ----
    int   epShow  = 0;         // show the EmpyPop box. OFF by default : a niche tracker only useful while
                               // farming ONE specific NM -- unlike ztShow, nothing auto-hides it per zone.
    float epScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float epX     = 0.80f;     // horizontal RIGHT edge (screen fraction) ; the box grows LEFTward from it as content
                               // widens, so the top-right corner stays put. epY = its TOP (grows down).
    float epY     = 0.25f;
    int   epColl  = 1;         // show the collectable counter row (23 of the 28 NMs have one)
    char  epTrack[32] = "briareus";   // tracked NM : the nms_gen.h KEY ("arch dynamis lord" -- spaces are real),
                                      // NOT an index. NMS[] is sorted by key, so an index would silently
                                      // re-point at a different NM the day the generated table gains an entry.
    TextStyle epText[EP_TE_COUNT];   // per-element typography : [EP_TITLE] [EP_POP] [EP_FROM] [EP_COLL]
    // ---- Timers module : self buff timers (exact durations from 0x063 type-9 ; same buff-icon atlas as Player/Party) ----
    int   tmShow  = 1;         // show the buff-timers box
    float tmScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float tmX     = 0.86f;     // horizontal LEFT edge (screen fraction) ; tmY = its TOP
    float tmY     = 0.30f;
    int   tmMax   = 16;        // max timers shown
    int   tmTitle = 1;         // show the column titles (Duration / Recast)
    BoxStyle tmBox;            // box appearance (frame / transparency / theme) -- shared bundle
    int   tmMerged = 1;        // 1 = Duration + Recast fused in ONE box (side by side) ; 0 = two independent boxes
    float tmRX    = 0.86f;     // Recast box position when SEPARATE (tmX/tmY = the Duration / fused box)
    float tmRY    = 0.44f;
    int   tmDurMode = TMDISP_ICON;   // Duration column display : icon / name / both
    int   tmRecMode = TMDISP_NAME;   // Recast   column display : name (default — the addon icon set isn't the real menu
                                     //            icons, so text reads cleaner) / icon / both
    float tmIconScale = 1.0f;        // Duration buff-icon size multiplier (0.5 .. 2.0 ; also grows the row height)
    float tmRowGap    = 1.0f;        // vertical row-spacing multiplier between each timer line (0.6 .. 3.0)
    int   tmOthers  = 1;             // Duration : 1 = show ALL buff-timer entries (incl. dual-box / others') ; 0 = only the
                                     //            LOCAL player's own buffs (cross-checked vs the memory buff list)
                                     //            LEGACY : superseded by tmBuffSrc (kept for old-config migration on load)
    int   tmBuffSrc = 3;             // Duration buff-SOURCE filter (your own always shown) : 0 = mine only,
                                     //   1 = mine + other players, 2 = mine + trusts, 3 = all. Trust vs player by name DB.
    int   tmSpAlert = 1;             // 1 = SP-ability buffs (both SP1/SP2, all jobs) blink HARD in their last minute
                                     //   (Soul Voice -> Nitro window, etc.) ; 0 = off
    int   tmMine    = 1;             // Duration : show BUFFS YOU cast on OTHER players (person name + ESTIMATED timer,
                                     //            base duration from tb_buff_gen -- no server timer exists for allies)
    int   tmAllyGroup = 1;           // buffs on allies : 1 = GROUP same-spell into one "Spell (AoE N)" row ;
                                     //            0 = one "Person - Spell" row PER ally (single-target Haste/Protect/... spread)
    int   tmFocusWarn = 60;          // FOCUS alerts : a "Hidden + focus" buff surfaces (red) when its remaining time drops
                                     //   below this many SECONDS (also fires immediately when it drops / is dispelled). 10..300.
    int   tmFocusHold = 60;          // FOCUS alerts : once a "Hidden + focus" buff is LOST, the red "OUT" row holds this many
                                     //   SECONDS then clears if not re-cast (a "Follow + focus" alert instead stays permanently). 5..300.
    TextStyle tmText[TM_TE_COUNT];   // Timers typography : [TM_HEADER] column titles  [TM_BODY] names + MM:SS
    // ---- DEBUFFS module : the target's debuffs DETACHED from the Target box into a Timers-style list (mob-name
    //      header + rows of icon / name / timer). Fed by party().target_debuffs (same data + colours + ??? logic
    //      as the in-box icons ; nothing new in the model). dbShow is the "independent" toggle : when ON, the
    //      Target box drops its debuff icons and this module draws them instead. ----
    int   dbShow  = 0;         // 0 = debuffs stay IN the Target box (tgtDebuffs) ; 1 = detached into this module
    float dbScale = 1.0f;      // size multiplier (0.5 .. 2.0)
    float dbX     = 0.80f;     // horizontal LEFT edge (screen fraction) ; dbY = its TOP
    float dbY     = 0.42f;
    int   dbMax   = 20;        // max debuff rows shown (1 .. 32)
    int   dbHeader = 1;        // show the mob-name header row
    int   dbDisp  = TMDISP_BOTH;     // row display : icon / name / icon+name (reuses the Timers TMDISP_ enum)
    float dbIconScale = 1.0f;        // debuff-icon size multiplier (0.5 .. 2.0 ; also grows the row height)
    float dbRowGap    = 1.0f;        // vertical row-spacing multiplier (0.6 .. 3.0)
    BoxStyle dbBox;                  // box appearance (frame / transparency / theme) -- shared bundle
    TextStyle dbText[DB_TE_COUNT];   // Debuffs typography : [DB_HEADER] mob name  [DB_NAME] debuff name  [DB_TIMER] timer
    // ---- Timers "track per job" : DISABLED entries (BLACKLIST ; empty = track everything, the default). Per
    //      job id 1..23, a list of keys. A key is a buff STATUS id (< TM_KEY_RECAST) OR TM_KEY_RECAST+recast_id
    //      (a cooldown). One checklist entry toggled OFF stores BOTH its keys. hud_timers hides a buff/recast
    //      whose key is present for the current main job. See job_track_gen.h for the per-job entry lists. ----
    static const unsigned TM_KEY_RECAST = 2000;   // recast keys start here ; buff-status keys are below it
    static const unsigned TM_KEY_ALLY   = 0x4000; // ALLIES-scope buff key = TM_KEY_ALLY | status (self-scope buff = raw status ; recasts are self only)
    static const unsigned TM_KEY_FOCUS  = 0x8000; // FOCUS key = TM_KEY_FOCUS | status : a critical buff (Haste/Refresh/Phalanx/Flurry...) -> keep a RED "missing" row on an ally who loses it until you re-cast
    static const int TM_TRACK_MAX = 512;          // max disabled keys per job (~ entries x2 ; full spellbooks are large)
    // the whole key scheme relies on the buff-status band (runtime-gated < 1024) staying BELOW the recast band, so a
    // status key can never collide with a TM_KEY_RECAST+recast key. If a buff status id >= 2000 is ever added this breaks.
    static_assert(1024 <= TM_KEY_RECAST, "buff status band must stay below the recast band (else tmTrackOff key collision)");
    unsigned short tmTrackOff[24][TM_TRACK_MAX];
    unsigned short tmTrackOffN[24] = { 0 };   // count per job (short : a full "default UFF" job preset is ~200 keys, over the old 255 byte cap)
    int   tmPreset = 0;              // migration version : 0 = never seeded ; 1 = RDM "Unfollow-Focus by default (except Haste/Refresh/Flurry/Phalanx)" preset applied. Bumped in load_config_from ; per-file so a saved profile keeps its manual edits.
    bool tm_track_off(int job, unsigned key) const {
        if (job < 1 || job > 23 || !key) return false;
        for (int i = 0; i < tmTrackOffN[job]; ++i) if (tmTrackOff[job][i] == key) return true;
        return false;
    }
    void tm_track_set(int job, unsigned key, bool off) {
        if (job < 1 || job > 23 || !key) return;
        int idx = -1; for (int i = 0; i < tmTrackOffN[job]; ++i) if (tmTrackOff[job][i] == key) { idx = i; break; }
        if (off) { if (idx < 0 && tmTrackOffN[job] < TM_TRACK_MAX) tmTrackOff[job][tmTrackOffN[job]++] = (unsigned short)key; }
        else if (idx >= 0) tmTrackOff[job][idx] = tmTrackOff[job][--tmTrackOffN[job]];
    }
    // Per-module box appearance (shared bundle ; Target/Player inline their own). Default = follow Party theme.
    BoxStyle scBox, tpBox, hlBox, pwBox, ztBox, mmBox, epBox;   // mmBox = the Minimap CLOCK box (day / moon header), not the map
    TextStyle mmText[MM_TE_COUNT];   // clock per-element typography : [MM_TIME] time, [MM_DAY] day, [MM_MOON] moon, [MM_REAL] real/GMT
    bool  mmPosSet = false;    // user-placed position (edit mode) ; else the layout default
    float mmX = 0.0f, mmY = 0.0f;   // top-left as a screen fraction when mmPosSet
    float mmScale = 1.0f;      // minimap size multiplier (0.50 .. 2.00 ; wheel resize in edit mode)
    float mmZoom  = 2.0f;      // minimap zoom (player-centred ; 1.0 = whole map fits, higher = zoomed in)
    int   mmShape = 0;         // 0 = square, 1 = round (circular clip)
    int   mmFrame = 1;         // frame : 0 = none, 1 = follow the party/alliance box theme, 2 = custom colour
    unsigned mmFrameColor = 0xFF6699BBu;   // custom frame colour (mmFrame == 2)
    float mmBgAlpha = 0.0f;    // backdrop opacity behind the map (0 = fully transparent, the game shows through)
    float mmMarkerScale = 1.0f;// entity/player marker size multiplier (0.5 .. 2.0)
    int   mmPC = 1, mmNPC = 1, mmMob = 1;  // per-type entity marker visibility
    int   mmTgtLine = 1;               // draw a line from you to your current target on the map (FFXIDB "target-line")
    unsigned mmTgtLineCol = 0xFFFF6A6Au;   // target-line colour
    int   mmRing = 0;                  // show a range ring (pull / aggro distance) centred on you
    float mmRingR = 20.0f;             // range-ring radius in game distance (yalms ; 3 .. 50)
    unsigned mmRingCol = 0xFF66E0FFu;  // range-ring colour
    // transient (not saved) : minimap rect as screen fractions + pending wheel, so the mouse callback can
    // zoom the map when the cursor is over it (outside edit mode). Written by the Minimap widget each frame.
    float mmHitX = 0.0f, mmHitY = 0.0f, mmHitW = 0.0f, mmHitH = 0.0f;
    int   mmWheel = 0;
    bool  mmVisible = false;
    bool  mmPreview = false;   // transient : the config live-preview is driving the minimap's position this frame
    int   plrBuffs   = 1;      // show the buff tray
    int   plrBuffMax = 24;     // max buff icons shown (1..32)
    float plrBarH    = 1.0f;   // fiole HEIGHT multiplier
    float plrBarW    = 1.0f;   // fiole WIDTH as a fraction of the inner width (0.50 .. 1.00)
    float plrBarGap  = 1.0f;   // spacing between the HP/MP/TP fioles (multiplier ; 0 = touching)
    float plrIconSz  = 1.0f;   // buff icon size multiplier
    float plrEmblemSz = 1.0f;  // job emblem size multiplier (icon ; the name/level text is sized via plrText[])
    TextStyle plrText[PLR_TE_COUNT];   // per-element typography : [PLR_NAME] name, [PLR_LVL] job/level line (Font/Size/Outline/Style/Colour)
    // ---- PER-BOX settings : index 0 = party, 1 = alliance 1, 2 = alliance 2 (independent) ----
    float barHeight[3] = { 1.0f, 1.0f, 1.0f };   // HP/MP/TP gauge HEIGHT scale, per box
    float barWidth[3]  = { 1.0f, 1.0f, 1.0f };   // HP/MP/TP gauge WIDTH scale, per box
    float badgeScale[3] = { 1.0f, 1.0f, 1.0f };  // job-badge box SIZE scale, per box (0.50 .. 2.00)
    int   gaugeStyle[3] = { 0, 0, 0 };           // gauge look, per box (0 Vial, 1 Bars, 2 Segments, 3 Minimal, 4 Sphere, 5 Ring, 6 Crystal, 7 Text)
    int   jobBadge[3]  = { 2, 2, 2 };            // job badge, per box (0 = off, 1 = main only, 2 = main + sub)
    bool  cast[3]      = { true, true, true };   // show the casting-spell line, per box
    bool  dist[3]   = { true, true, true };   // show the distance number, per box (0 = party, 1 = ally 1, 2 = ally 2)
    BoxLayout box[3];          // 0 = party (+cost), 1 = alliance 1, 2 = alliance 2 (independent)
    bool  border[3] = { true, true, true };   // per-box window-skin border/chrome on/off (0=party, 1=alliance1, 2=alliance2)
    bool  borderCost = true;   // the floating Cost MP / Next box border/chrome on/off (independent of the party box)
    bool  animHP = true;       // HP gauges : critical-low blink animation on/off
    bool  animTP = true;       // TP gauges : WS-ready (>= 1000) pulse animation on/off
    bool  editLayout = false;  // layout edit mode : boxes draggable / resizable on the live game
    int   wheel = 0;           // pending wheel steps (mouse slot -> consumed by the hovered box in edit mode)
    // reference LINE per native-party size : the party box grows UP to this Y (fraction of screen height)
    // so the game's native party window is covered. The native top differs by member count, so there is
    // ONE line per count : [i] = (i+1) players. -1 = unset (no grow for that count).
    float partyRef[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    float partyBottomY = -1.0f;   // line 0 : the BOTTOM of the native party window -- a VISUAL reference only
                                  // (documents where the native party ends). -1 = unset.
    float partyRefX[2] = { -1.0f, -1.0f };   // two VERTICAL reference markers : the LEFT [0] and RIGHT [1] edges of
                                             // the native party window (fraction of screen WIDTH). -1 = unset.
    // four FIXED horizontal markers for the native ALLIANCE windows (their size does NOT vary with member
    // count) : [0] = alliance 1 TOP, [1] = alliance 1 BOTTOM, [2] = alliance 2 TOP, [3] = alliance 2 BOTTOM.
    float allyRefY[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    // ---- user-drawn zones ----
    GuideGroup guideGroup[GUIDE_GROUPS_MAX];
    int        guideGroupCount = 0;
    float      zonePanelX = -1.0f, zonePanelY = -1.0f;   // draggable Zones panel top-left (fraction ; -1 = default top-right)
    // ---- Global ----
    int   lang = 0;            // config UI language : 0 = English, 1 = French (toggle in the config header)
};

UiConfig& ui_config();         // the singleton

// ---- P3 : guide ZONES = the rectangle each group forms from its rules (H rules -> top/bottom, V rules ->
// left/right ; a missing axis spans the whole screen). Permissions (allow[ZPERM_*]) say which box may sit on
// a zone -- a box is pushed OUT of any zone that forbids its type. ----
int  guide_zones(float sw, float sh, float* x, float* y, float* w, float* h, int* group, int cap);   // -> count
void guide_push_out(int perm, float sw, float sh, float& ex, float& ey, float ew, float eh);          // clamp a box out of forbidden zones

// modifier-key state (fed by the plugin's keyboard hook -> reliable in the render thread, unlike
// GetAsyncKeyState). Used for edit-mode axis-lock : Shift = horizontal only, Ctrl = vertical only.
void edit_set_modifiers(bool shift, bool ctrl, bool alt);
bool edit_shift();
bool edit_ctrl();
bool edit_alt();   // Alt held during an edit-mode drag = FREE placement (ignore box-vs-box + keep-out zones)

// persistence : skinTheme / fontFace / per-box position+scale are saved to disk and restored.
void load_ui_config();         // called once at startup
void save_ui_config();         // call after a change (stepper / edit-mode exit / reset)
void reset_ui_config();        // restore ALL defaults (theme/font/boxes) + save  (general Default)
void reset_boxes();            // restore only box positions + sizes + save  (edit-mode Default)

// ---- profiles : snapshot the WHOLE UiConfig under a name (files aiohud_profiles\<name>.txt, same
// key=value format as the live config). Saving more modules later just means more keys in UiConfig --
// profiles serialize all of it, no per-module work here. ----
int         profile_count();
const char* profile_name(int idx);                 // 0-based ; "" if out of range
void        profile_refresh();                     // rescan the folder (Profile tab open / after save/delete)
bool        profile_save(const char* name);        // write the CURRENT config under <name> (create or overwrite)
bool        profile_load(const char* name);        // load <name> into the live config (+ persist as current)
bool        profile_delete(const char* name);
bool        profile_exists(const char* name);
void        profile_mark_clean();                  // snapshot the current config as "saved" (call on load/save)
bool        profile_dirty();                       // true if the live config differs from that snapshot -> unsaved changes
// the LAST loaded/saved profile is remembered on disk and auto-applied at startup, so a relaunch
// comes back on the same profile (set automatically by profile_load / profile_save).
void        set_active_profile(const char* name);
const char* active_profile_name();                 // "" if none
// character-bound profiles : "Name Main/Sub" default name + auto-load on Name/Main/Sub match (else this character's last).
bool        profile_default_name(char* out, int cap);   // "Name Main/Sub" for the live character (false if not readable)
void        record_char_profile(const char* prof);      // remember that the current character (manually) loaded/saved `prof`
void        profile_autoload_tick();                    // call each frame : auto-switch profile on login / job change
// A bulk config load (startup, profile load, reset) replaces every box scale + position at once. The HUD
// must ADOPT those as the new baseline instead of treating them as a live resize (which would re-anchor
// the boxes and falsely mark the profile "modified"). Set on any such load ; consumed once by the HUD.
void        request_scale_baseline_reset();
bool        take_scale_baseline_reset();           // returns + clears the flag

// selectable font faces for the Font control (index 0 = "Default" = keep the layout font).
int         ui_font_count();
const char* ui_font_label(int idx);   // display label
const char* ui_font_face(int idx);    // GDI face name ("" for Default)

} // namespace aio
