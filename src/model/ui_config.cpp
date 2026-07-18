// ui_config.cpp -- see ui_config.h.
#include "model/ui_config.h"
#include "model/paths.h"
#include "model/job_track_gen.h"
#include "model/game_mem.h"   // read_player : current character name + main/sub job -> default profile name + auto-load

#ifndef AIOHUD_VERSION
#define AIOHUD_VERSION "dev"   // injected by build.bat / CI from the git tag ; fallback for ad-hoc builds
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <windows.h>
#include <locale.h>

// Force C-locale numbers (dot decimals) for config I/O, restored on scope exit. Some systems / other addons
// set the process locale to one with a COMMA decimal (French-Canadian, French, German...) -> then sprintf /
// sscanf "%f" would write/read "0,5" and every saved position/scale would be corrupt. This makes AioHud's
// config files always dot-decimal + portable, whatever the OS locale. AioHud is single-threaded (game thread).
namespace { struct CNumLoc {
    char saved[64];
    CNumLoc()  { const char* p = setlocale(LC_NUMERIC, NULL); lstrcpynA(saved, p ? p : "C", sizeof(saved)); setlocale(LC_NUMERIC, "C"); }
    ~CNumLoc() { setlocale(LC_NUMERIC, saved); }
}; }

namespace aio {

UiConfig& ui_config() { static UiConfig c; return c; }

// Paths derived from the plugin's own location (runtime) -> work on any install, not just the dev box.
static const char* config_path() { static char b[260]; if (!b[0]) plugin_path(b, 260, "data\\config.txt");        return b; }
static const char* profile_dir() { static char b[260]; if (!b[0]) plugin_path(b, 260, "data\\profiles");          return b; }
static const char* active_path() { static char b[260]; if (!b[0]) plugin_path(b, 260, "data\\active.txt");        return b; }

static void profile_path(const char* name, char* out, int cap);   // (defined below)
static char g_active[48] = {0};   // last loaded/saved profile name -> persisted + auto-applied at startup

static bool g_scaleReset = true;   // true initially -> the HUD adopts the loaded scales as baseline (no re-anchor) on the first frame
void request_scale_baseline_reset() { g_scaleReset = true; }
bool take_scale_baseline_reset() { bool r = g_scaleReset; g_scaleReset = false; return r; }

void set_active_profile(const char* name) {
    if (!name) name = "";
    strncpy(g_active, name, sizeof(g_active) - 1); g_active[sizeof(g_active) - 1] = 0;
    FILE* f = fopen(active_path(), "w"); if (f) { fputs(g_active, f); fclose(f); }
}
const char* active_profile_name() { return g_active; }

// ---- character-bound profiles : default name "Name Main/Sub" + auto-load on match ----
static const char* job_abbrev(int j) {
    static const char* const JAB[24] = { "-","WAR","MNK","WHM","BLM","RDM","THF","PLD","DRK","BST","BRD","RNG","SAM",
                                         "NIN","DRG","SMN","BLU","COR","PUP","DNC","SCH","GEO","RUN","MON" };
    return (j >= 1 && j <= 23) ? JAB[j] : "";
}
// "Name Main/Sub" (or "Name Main" with no sub). false if the character isn't readable yet.
bool profile_default_name(char* out, int cap) {
    if (cap) out[0] = 0;
    PlayerInfo me; if (!read_player(me) || !me.name[0] || me.mjob < 1 || me.mjob > 23) return false;
    if (me.sjob >= 1 && me.sjob <= 23) _snprintf(out, cap, "%s %s-%s", me.name, job_abbrev(me.mjob), job_abbrev(me.sjob));   // '-' not '/' : '/' is filename-illegal (would %-encode to %2F)
    else                               _snprintf(out, cap, "%s %s", me.name, job_abbrev(me.mjob));
    out[cap - 1] = 0; return true;
}
// per-character "last profile this character loaded" -> the fallback when no "Name Main/Sub" profile exists.
static const char* charprof_path() { static char b[260]; if (!b[0]) plugin_path(b, 260, "data\\charprofiles.txt"); return b; }
static char g_charProf[32][2][48]; static int g_charProfN = 0; static bool g_charProfLoaded = false;
static void charprof_load() {
    if (g_charProfLoaded) return; g_charProfLoaded = true; g_charProfN = 0;
    FILE* f = fopen(charprof_path(), "r"); if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f) && g_charProfN < 32) {
        char* tab = strchr(line, '\t'); if (!tab) continue; *tab = 0;
        char* prof = tab + 1; size_t n = strlen(prof); while (n && (prof[n-1] == '\n' || prof[n-1] == '\r')) prof[--n] = 0;
        if (!line[0] || !prof[0]) continue;
        strncpy(g_charProf[g_charProfN][0], line, 47); g_charProf[g_charProfN][0][47] = 0;
        strncpy(g_charProf[g_charProfN][1], prof, 47); g_charProf[g_charProfN][1][47] = 0;
        ++g_charProfN;
    }
    fclose(f);
}
static void charprof_save() {
    FILE* f = fopen(charprof_path(), "w"); if (!f) return;
    for (int i = 0; i < g_charProfN; ++i) fprintf(f, "%s\t%s\n", g_charProf[i][0], g_charProf[i][1]);
    fclose(f);
}
// record that the CURRENT character just (manually) loaded/saved `prof`. Called from the UI load/save sites only.
void record_char_profile(const char* prof) {
    if (!prof || !prof[0]) return;
    PlayerInfo me; if (!read_player(me) || !me.name[0]) return;
    charprof_load();
    int s = -1; for (int i = 0; i < g_charProfN; ++i) if (strcmp(g_charProf[i][0], me.name) == 0) { s = i; break; }
    if (s < 0) { if (g_charProfN >= 32) return; s = g_charProfN++; strncpy(g_charProf[s][0], me.name, 47); g_charProf[s][0][47] = 0; }
    strncpy(g_charProf[s][1], prof, 47); g_charProf[s][1][47] = 0;
    charprof_save();
}
static const char* last_char_profile(const char* charName) {
    charprof_load();
    for (int i = 0; i < g_charProfN; ++i) if (strcmp(g_charProf[i][0], charName) == 0) return g_charProf[i][1];
    return 0;
}

// ---- core (de)serialization : the live config <-> a key=value file at `path`. The named-profile
// API and the single live-config file both go through these, so they always share one format. ----
static void save_config_to(const char* path) {
    CNumLoc _cnl;   // dot decimals regardless of the OS locale
    FILE* f = fopen(path, "w"); if (!f) return;
    UiConfig& c = ui_config();
    fprintf(f, "partyShow=%d\n", c.partyShow);
    fprintf(f, "allyShow=%d\n", c.allyShow);
    fprintf(f, "tgtShow=%d\n", c.tgtShow);
    fprintf(f, "plrShow=%d\n", c.plrShow);
    fprintf(f, "skinTheme=%d\n", c.skinTheme);
    fprintf(f, "skinLum=%.3f\n", c.skinLum);
    fprintf(f, "skinHue=%08X\n", c.skinHue);
    fprintf(f, "skinBoxAlpha=%.3f\n", c.skinBoxAlpha);
    fprintf(f, "allyThemeCopy=%d\n", c.allyThemeCopy);
    fprintf(f, "allyTheme=%d\n", c.allyTheme);
    fprintf(f, "allyLum=%.3f\n", c.allyLum);
    fprintf(f, "allyHue=%08X\n", c.allyHue);
    fprintf(f, "allyBoxAlpha=%.3f\n", c.allyBoxAlpha);
    fprintf(f, "fontFace=%d\n", c.fontFace);
    fprintf(f, "buffScale=%.4f\n", c.buffScale);
    fprintf(f, "buffMax=%d\n", c.buffMax);
    fprintf(f, "buffRows=%d\n", c.buffRows);
    fprintf(f, "uiStyle=%d\n", c.uiStyle);
    fprintf(f, "uiColor=%d\n", c.uiColor);
    fprintf(f, "uiAccent=%08X\n", c.uiAccent);
    fprintf(f, "uiCursor=%d\n", c.uiCursor);
    fprintf(f, "hidePeekMode=%d\n", c.hidePeekMode);
    fprintf(f, "cursorScale=%.4f\n", c.cursorScale);
    fprintf(f, "tgtBox=%d\n", c.tgtBox);
    fprintf(f, "tgtBoxAlpha=%.4f\n", c.tgtBoxAlpha);
    fprintf(f, "tgtThemeCopy=%d\n", c.tgtThemeCopy);
    fprintf(f, "tgtTheme=%d\n", c.tgtTheme);
    fprintf(f, "tgtLum=%.3f\n", c.tgtLum);
    fprintf(f, "tgtHue=%08X\n", c.tgtHue);
    fprintf(f, "tgtScale=%.4f\n", c.tgtScale);
    fprintf(f, "tgtNameHostile=%d\n", c.tgtNameHostile);
    fprintf(f, "tgtSpeed=%d\n", c.tgtSpeed);
    fprintf(f, "tgtSpeedIcon=%d\n", c.tgtSpeedIcon);
    fprintf(f, "tgtTH=%d\n", c.tgtTH);
    fprintf(f, "tgtThIcon=%d\n", c.tgtThIcon);
    fprintf(f, "tgtRange=%d\n", c.tgtRange);
    fprintf(f, "tgtCast=%d\n", c.tgtCast);
    fprintf(f, "tgtCastDemo=%d\n", c.tgtCastDemo);
    fprintf(f, "tgtSubPos=%d\n", c.tgtSubPos);
    fprintf(f, "tgtSub=%d\n", c.tgtSub);
    fprintf(f, "plrCast=%d\n", c.plrCast);
    fprintf(f, "plrCastDemo=%d\n", c.plrCastDemo);
    fprintf(f, "tgtBuffMax=%d\n", c.tgtBuffMax);
    fprintf(f, "tgtDebuffs=%d\n", c.tgtDebuffs);
    fprintf(f, "tgtBuffPos=%d\n", c.tgtBuffPos);
    fprintf(f, "tgtTimers=%d\n", c.tgtTimers);
    for (int i = 0; i < TGT_TE_COUNT; ++i) {
        const TextStyle& ts = c.tgtText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "tgtText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "tgtBars=%.4f,%.4f,%.4f\n", c.tgtBarH, c.tgtBarW, c.tgtIconSz);
    fprintf(f, "tgtDetailIconSz=%.4f\n", c.tgtDetailIconSz);
    fprintf(f, "tgtRangeH=%.4f\n", c.tgtRangeH);
    fprintf(f, "tgtRangeMin=%d\n", c.tgtRangeMin);
    fprintf(f, "tgtPos=%d,%.5f,%.5f\n", c.tgtPosSet ? 1 : 0, c.tgtX, c.tgtY);
    fprintf(f, "tgtCenter=%d,%d\n", c.tgtCenterH, c.tgtCenterV);
    fprintf(f, "plrBox=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", c.plrBox, c.plrEmblem, c.plrName, c.plrLvl, c.plrHp, c.plrMp, c.plrTp, c.plrBuffs, c.plrBuffMax, c.plrSpeed, c.plrGil, c.plrEquip);   // plrSpeed/plrGil/plrEquip appended last -> old shorter lines still parse
    fprintf(f, "plrEq=%.4f,%d,%08X,%d,%d,%08X\n", c.plrEqCell, c.plrEqThemeBorder, c.plrEqColor, c.plrEqPlace, c.plrEqCellBgCustom, c.plrEqCellBg);   // equipment viewer : cell size / theme-border / custom border / placement / cell-bg custom / cell-bg colour
    fprintf(f, "plrEqDet=%d,%d,%.5f,%.5f,%.4f,%d\n", c.plrEquipDetach, c.plrEquipPosSet ? 1 : 0, c.plrEquipX, c.plrEquipY, c.plrEquipScale, c.plrEqGilPlace);   // standalone equipment : detach / posSet / x / y / scale / gil placement
    fprintf(f, "mm=%d,%d,%.5f,%.5f,%.4f,%.4f\n", c.mmShow, c.mmPosSet ? 1 : 0, c.mmX, c.mmY, c.mmScale, c.mmZoom);   // minimap : show / posSet / x / y / scale / zoom
    fprintf(f, "mm2=%d,%d,%08X,%.3f,%.3f,%d,%d,%d\n", c.mmShape, c.mmFrame, c.mmFrameColor, c.mmBgAlpha, c.mmMarkerScale, c.mmPC, c.mmNPC, c.mmMob);   // shape / frame / frameColour / bgAlpha / markerScale / PC / NPC / mob
    fprintf(f, "mm4=%d,%08X,%d,%.2f,%08X\n", c.mmTgtLine, c.mmTgtLineCol, c.mmRing, c.mmRingR, c.mmRingCol);   // target-line on/colour ; range-ring on/radius/colour  (was mm3= -> collided with the clock line below, so it never reloaded)
    fprintf(f, "mm3=%d,%d,%d,%d,%d,%.3f,%d\n", c.mmClock, c.mmClkTime, c.mmClkDay, c.mmClkMoon, c.mmClkReal, c.mmMapSize, c.mmClockPos);   // clock : on/time/day/moon/real + independent map size + header placement
    fprintf(f, "mm5=%.3f,%.3f,%d,%.3f\n", c.mmBezelW, c.mmCardSz, c.mmBezel, c.mmSqBorder);   // round bezel width / round cardinal size / round bezel on / square border width
    fprintf(f, "ws=%d,%.3f,%.3f,%.3f,%d,%d\n", c.wsShow, c.wsScale, c.wsX, c.wsY, c.wsFont, c.wsFx);   // arcade WS popup
    fprintf(f, "sc=%d,%.3f,%.4f,%.4f,%d\n", c.scShow, c.scScale, c.scX, c.scY, c.scNearby);   // skillchains box (+ display scope)
    fprintf(f, "tp=%d,%.3f,%.4f,%.4f,%d,%d\n", c.tpShow, c.tpScale, c.tpX, c.tpY, c.tpCount, c.tpIcon);   // treasure pool box
    for (int i = 0; i < TP_TE_COUNT; ++i) {                                       // treasure pool : per-element typography
        const TextStyle& ts = c.tpText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "tpText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "hl=%d,%.3f,%.4f,%.4f,%d,%d,%d\n", c.hlShow, c.hlScale, c.hlX, c.hlY, c.hlCount, c.hlDist, c.hlTgt);   // hate list box
    for (int i = 0; i < HL_TE_COUNT; ++i) {                                       // hate list : per-element typography
        const TextStyle& ts = c.hlText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "hlText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "pw=%d,%.3f,%.4f,%.4f,%d,%d,%d,%d\n", c.pwShow, c.pwScale, c.pwX, c.pwY, c.pwMode, c.pwRate, c.pwLayout, c.pwDisplay);   // pointwatch box (+ layout/display)
    for (int i = 0; i < PW_TE_COUNT; ++i) {                                       // pointwatch : per-element typography
        const TextStyle& ts = c.pwText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "pwText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "grim=%d,%.3f,%.4f,%.4f,%d\n", c.grimShow, c.grimScale, c.grimX, c.grimY, c.grimArt);   // grimoire box
    for (int i = 0; i < GRIM_TE_COUNT; ++i) {                                     // grimoire : per-element typography
        const TextStyle& ts = c.grimText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "grimText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "zt=%d,%.3f,%.4f,%.4f,%d,%d\n", c.ztShow, c.ztScale, c.ztX, c.ztY, c.ztVariant, c.ztHeader);   // zone tracker box (+ title toggle)
    fprintf(f, "ztsheol=%d,%d,%d\n", c.ztSheolSeg, c.ztSheolRes, c.ztSheolJoke);   // Sheol : segments / resistances / cruel joke
    fprintf(f, "ztlimbus=%d,%d,%d,%d,%d,%.2f,%.2f\n", c.ztLbFloor, c.ztLbCur, c.ztLbRun, c.ztLbChips, c.ztLbName, c.ztLbBarW, c.ztLbBarH);   // Limbus : floor-on-gauge / currencies / run / coffer dots / name row / gauge W / gauge H
    fprintf(f, "ztdyn=%d,%d,%.2f,%.2f,%.2f\n", c.ztDyTimer, c.ztDyKi, c.ztDyBarW, c.ztDyBarH, c.ztDyDot);   // Dynamis : timer / key items / bar W / bar H / dot size
    fprintf(f, "ztaby=%d,%d,%.2f,%.2f,%.2f,%.2f\n", c.ztAbTimer, c.ztAbLights, c.ztAbBarW, c.ztAbBarH, c.ztAbLightW, c.ztAbLightH);   // Abyssea : timer / lights / bar W / bar H / light W / light H
    fprintf(f, "ztomen=%d,%d,%d\n", c.ztOmObj, c.ztOmCount, c.ztOmRows);   // Omen : floor objective / omen+bonus / objective rows
    fprintf(f, "ztnyzul=%d,%d,%d,%d,%d,%d,%d\n", c.ztNyFloor, c.ztNyTime, c.ztNyObj, c.ztNyRestr, c.ztNyComp, c.ztNyRate, c.ztNyTok);   // Nyzul : per-row toggles
    fprintf(f, "ztsheol2=%d,%.2f,%.2f\n", c.ztShFam, c.ztShIcon, c.ztShDot);   // Sheol : family row / weapon icon size / element puck size
    fprintf(f, "tm=%d,%.3f,%.4f,%.4f,%d,%d,%d,%d,%.4f,%.4f,%d,%d,%d,%.3f,%d,%d,%d,%.3f\n", c.tmShow, c.tmScale, c.tmX, c.tmY, c.tmMax, c.tmTitle, c.tmBox.on, c.tmMerged, c.tmRX, c.tmRY, c.tmDurMode, c.tmRecMode, c.tmOthers, c.tmIconScale, c.tmMine, c.tmBuffSrc, c.tmSpAlert, c.tmRowGap);   // Timers box (+ box.on/merge/recast-pos/display modes/others/icon-scale/buffs-on-allies/buff-source/SP-alert/row-spacing)
    fprintf(f, "db=%d,%.3f,%.4f,%.4f,%d,%d,%d,%.3f,%.3f\n", c.dbShow, c.dbScale, c.dbX, c.dbY, c.dbMax, c.dbHeader, c.dbDisp, c.dbIconScale, c.dbRowGap);   // Debuffs module (detached target debuffs)
    {   // per-module box appearance (shared BoxStyle : frame / transparency / theme / hue / luminosity)
        auto sb = [&](const char* k, const BoxStyle& b) { fprintf(f, "%s=%d,%.4f,%d,%d,%.4f,%08X,%d\n", k, b.on, b.alpha, b.themeCopy, b.theme, b.lum, b.hue, b.border); };   // border = trailing field (old 6-field configs load with border=default)
        sb("scbox", c.scBox); sb("tpbox", c.tpBox); sb("hlbox", c.hlBox); sb("pwbox", c.pwBox); sb("ztbox", c.ztBox); sb("tmbox", c.tmBox); sb("mmbox", c.mmBox); sb("epbox", c.epBox); sb("dbbox", c.dbBox); sb("plreqbox", c.plrEqBox);
    }
    fprintf(f, "ep=%d,%.3f,%.4f,%.4f,%d\n", c.epShow, c.epScale, c.epX, c.epY, c.epColl);   // EmpyPop box (+ collectable row)
    fprintf(f, "eptrack=%s\n", c.epTrack);   // the tracked NM KEY -- its OWN line : keys contain spaces
                                             // ("arch dynamis lord"), so it must never share a CSV line.
    for (int i = 0; i < EP_TE_COUNT; ++i) {                                       // EmpyPop : per-element typography
        const TextStyle& ts = c.epText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "epText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    for (int i = 0; i < ZT_TE_COUNT; ++i) {                                       // zone tracker : per-element typography
        const TextStyle& ts = c.ztText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "ztText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    for (int i = 0; i < TM_TE_COUNT; ++i) {                                       // Timers : per-element typography
        const TextStyle& ts = c.tmText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "tmText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    for (int i = 0; i < DB_TE_COUNT; ++i) {                                       // Debuffs : per-element typography
        const TextStyle& ts = c.dbText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "dbText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "tmAllyGroup=%d\n", c.tmAllyGroup);   // buffs on allies : group same-spell into (AoE N) vs one row per ally
    fprintf(f, "tmFocus=%d,%d\n", c.tmFocusWarn, c.tmFocusHold);   // focus alert : warn-threshold + hold-after-loss (seconds)
    fprintf(f, "tmPreset=%d\n", c.tmPreset);         // track-preset seed version (see apply_rdm_uff_preset)
    for (int j = 1; j <= 23; ++j) if (c.tmTrackOffN[j] > 0) {                     // Timers "track per job" : disabled keys (blacklist ; only non-empty jobs written)
        fprintf(f, "tmTrkOff%d=", j);
        for (int i = 0; i < c.tmTrackOffN[j]; ++i) fprintf(f, "%s%u", i ? "," : "", (unsigned)c.tmTrackOff[j][i]);
        fprintf(f, "\n");
    }
    fprintf(f, "sc2=%d,%d,%d,%d,%d,%.4f\n", c.scTimer, c.scStep, c.scProps, c.scList, c.scTitle, c.scListGap);   // skillchains : element toggles (title + WS-list spacing appended)
    for (int i = 0; i < SC_TE_COUNT; ++i) {                                       // skillchains : per-element typography
        const TextStyle& ts = c.scText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "scText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "wscol=%08X,%08X,%08X\n", c.wsNameCol, c.wsDmgCol1, c.wsDmgCol2);
    fprintf(f, "plrSizes=%.4f,%.4f,%.4f,%.4f,%.4f\n", c.plrBoxAlpha, c.plrScale, c.plrBarH, c.plrBarW, c.plrIconSz);
    fprintf(f, "plrEmblemSz=%.4f\n", c.plrEmblemSz);
    fprintf(f, "plrBarGap=%.4f\n", c.plrBarGap);
    for (int i = 0; i < PLR_TE_COUNT; ++i) {
        const TextStyle& ts = c.plrText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "plrText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    for (int i = 0; i < MM_TE_COUNT; ++i) {
        const TextStyle& ts = c.mmText[i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "mmText%d=%d,%.4f,%.4f,%d,%08X\n", i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    fprintf(f, "plrTheme=%d,%d\n", c.plrThemeCopy, c.plrTheme);
    fprintf(f, "plrHue=%08X\n", c.plrHue);
    fprintf(f, "plrLum=%.3f\n", c.plrLum);
    fprintf(f, "plrPos=%d,%.5f,%.5f\n", c.plrPosSet ? 1 : 0, c.plrX, c.plrY);
    fprintf(f, "plrCenter=%d,%d\n", c.plrCenterH, c.plrCenterV);
    fprintf(f, "barHeight=%.4f,%.4f,%.4f\n", c.barHeight[0], c.barHeight[1], c.barHeight[2]);
    fprintf(f, "barWidth=%.4f,%.4f,%.4f\n", c.barWidth[0], c.barWidth[1], c.barWidth[2]);
    fprintf(f, "badgeScale=%.4f,%.4f,%.4f\n", c.badgeScale[0], c.badgeScale[1], c.badgeScale[2]);
    fprintf(f, "gaugeStyle=%d,%d,%d\n", c.gaugeStyle[0], c.gaugeStyle[1], c.gaugeStyle[2]);
    fprintf(f, "jobBadge=%d,%d,%d\n", c.jobBadge[0], c.jobBadge[1], c.jobBadge[2]);
    fprintf(f, "cast=%d,%d,%d\n", c.cast[0] ? 1 : 0, c.cast[1] ? 1 : 0, c.cast[2] ? 1 : 0);
    fprintf(f, "dist=%d,%d,%d\n", c.dist[0] ? 1 : 0, c.dist[1] ? 1 : 0, c.dist[2] ? 1 : 0);
    fprintf(f, "distcol=%08X,%08X,%08X\n", c.distColClose, c.distColNormal, c.distColFar);   // distance-zone colours : Close / Normal / Far
    fprintf(f, "lang=%d\n", c.lang);
    fprintf(f, "partyRef=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n", c.partyRef[0], c.partyRef[1], c.partyRef[2], c.partyRef[3], c.partyRef[4], c.partyRef[5]);
    fprintf(f, "partyBottom=%.5f\n", c.partyBottomY);
    fprintf(f, "partyRefX=%.5f,%.5f\n", c.partyRefX[0], c.partyRefX[1]);
    fprintf(f, "allyRef=%.5f,%.5f,%.5f,%.5f\n", c.allyRefY[0], c.allyRefY[1], c.allyRefY[2], c.allyRefY[3]);
    fprintf(f, "zonePanel=%.5f,%.5f\n", c.zonePanelX, c.zonePanelY);
    for (int i = 0; i < c.guideGroupCount; ++i)   // user-drawn zones : rect + permissions + role ; name LAST (comma-safe).
        fprintf(f, "zone=%.5f,%.5f,%.5f,%.5f,%d,%d,%d,%d,%s\n", c.guideGroup[i].x, c.guideGroup[i].y, c.guideGroup[i].w, c.guideGroup[i].h,
                c.guideGroup[i].allow[0] ? 1 : 0, c.guideGroup[i].allow[1] ? 1 : 0, c.guideGroup[i].allow[2] ? 1 : 0,
                c.guideGroup[i].role | (c.guideGroup[i].allow[ZPERM_TARGET] ? 0x100 : 0), c.guideGroup[i].name);   // Target-allow packed into role bit 8 (backward compatible)
    fprintf(f, "border=%d,%d,%d,%d\n", c.border[0] ? 1 : 0, c.border[1] ? 1 : 0, c.border[2] ? 1 : 0, c.borderCost ? 1 : 0);
    fprintf(f, "anim=%d,%d\n", c.animHP ? 1 : 0, c.animTP ? 1 : 0);
    for (int g = 0; g < 2; ++g) for (int i = 0; i < TE_COUNT; ++i) {
        const TextStyle& ts = c.text[g][i];
        int fl = (ts.bold ? 1 : 0) | (ts.italic ? 2 : 0) | (ts.upper ? 4 : 0) | (ts.colorOn ? 8 : 0);
        fprintf(f, "text%c%d=%d,%.4f,%.4f,%d,%08X\n", g == 0 ? 'P' : 'A', i, ts.face, ts.size, ts.outline, fl, ts.color);
    }
    for (int i = 0; i < 3; ++i)
        fprintf(f, "box%d=%d,%.5f,%.5f,%.4f\n", i, c.box[i].posSet ? 1 : 0, c.box[i].x, c.box[i].y, c.box[i].scale);
    fclose(f);
}

static void parse_box(const char* s, BoxStyle& b) {   // "on,alpha,themeCopy,theme,lum,hue[,border]" -> BoxStyle (keeps defaults on short lines)
    int on = b.on, tc = b.themeCopy, th = b.theme, bd = b.border; float al = b.alpha, lm = b.lum; unsigned hu = b.hue;
    const int n = sscanf(s, "%d,%f,%d,%d,%f,%x,%d", &on, &al, &tc, &th, &lm, &hu, &bd);
    if (n >= 1) { b.on = on; b.alpha = al; b.themeCopy = tc; b.theme = th; b.lum = lm; b.hue = hu; if (n >= 7) b.border = bd; }   // border absent (old config) -> keep default (1)
}

// One-time seed : RDM (job 5) starts every spell EXCEPT Haste/Refresh/Flurry/Phalanx in "Unfollow-Focus"
// (hidden from the list, but a RED alert pops when it drops or has < 1 min left, on both Self and Allies).
// Applied per config/profile file, gated by tmPreset so a saved profile keeps the user's later manual edits.
static void apply_rdm_uff_preset(UiConfig& c) {
    const int RDM = 5;
    int n = 0; const JobBuff* jb = job_track(RDM, n);
    for (int i = 0; i < n; ++i) {
        const unsigned st = jb[i].status;
        if (!st) continue;                                              // recast-only entry (nukes/cures) : nothing to "focus" on -> leave it Follow
        const unsigned char cat = jb[i].cat;
        if (!(cat <= TC_DEFENSE || cat == TC_ENHANCE)) continue;        // only self/ally buffs (Refresh..Defensive + Enhancing) ; enfeebles/nukes/dark aren't tracked buffs
        if (st == 33 || st == 43 || st == 581 || st == 116) continue;   // Haste / Refresh / Flurry / Phalanx : the ones we DO want followed
        // Unfollow-Focus = hidden key + focus key, for both scopes (self keys : raw status + its recast ; ally keys : TM_KEY_ALLY | ...)
        c.tm_track_set(RDM, st, true);                                                                 // self : hidden
        if (jb[i].recast) c.tm_track_set(RDM, UiConfig::TM_KEY_RECAST + jb[i].recast, true);           // self : hide its recast row too
        c.tm_track_set(RDM, UiConfig::TM_KEY_FOCUS | st, true);                                        // self : focus (status mirror the hud reads)
        if (jb[i].recast) c.tm_track_set(RDM, UiConfig::TM_KEY_FOCUS | (UiConfig::TM_KEY_RECAST + jb[i].recast), true);   // self : focus (per-entry key the config UI reads)
        c.tm_track_set(RDM, UiConfig::TM_KEY_ALLY | st, true);                                         // allies : hidden
        c.tm_track_set(RDM, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | st, true);                // allies : focus
    }
}

// EmpyPop's config lines, parsed OUT-OF-LINE. Not a style choice : load_config_from's else-if chain is one
// expression whose nesting depth is its length, and it already sits AT MSVC's limit -- adding four branches
// inline blew C1061 ("blocks nested too deeply") in the `zone=` parser at the far end of the chain. A handler
// called with `continue` costs the chain ZERO depth. Do this for the next module too, rather than compacting
// lines to buy a few levels. Returns true if `line` was consumed.
static bool parse_ep_line(const char* line, UiConfig& c) {
    int idx, v, v1; float fv, f1; unsigned uc;
    if (strncmp(line, "ep=", 3) == 0) {
        int sh = 0, cl = 1; float scl = 1.0f, x = 0.80f, y = 0.25f;
        const int n = sscanf(line + 3, "%d,%f,%f,%f,%d", &sh, &scl, &x, &y, &cl);
        if (n >= 1) { c.epShow = sh; if (n >= 2) c.epScale = scl; if (n >= 3) c.epX = x; if (n >= 4) c.epY = y; if (n >= 5) c.epColl = cl; }
        return true;
    }
    if (strncmp(line, "eptrack=", 8) == 0) {
        // Rest-of-line, NOT sscanf("%s") : an NM key may contain spaces ("arch dynamis lord"), which %s would
        // truncate at the first one. fgets keeps the newline -> strip it, or the key never matches nm_by_key.
        lstrcpynA(c.epTrack, line + 8, sizeof(c.epTrack));
        size_t n = strlen(c.epTrack);
        while (n && (c.epTrack[n-1] == '\n' || c.epTrack[n-1] == '\r')) c.epTrack[--n] = 0;
        return true;
    }
    if (sscanf(line, "epText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < EP_TE_COUNT) {
        TextStyle& ts = c.epText[idx];
        ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
        ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        return true;
    }
    if (!strncmp(line, "epbox=", 6)) { parse_box(line + 6, c.epBox); return true; }
    return false;
}

// Debuffs-module config lines, parsed OUT-OF-LINE (same C1061 nesting reason as parse_ep_line).
static bool parse_db_line(const char* line, UiConfig& c) {
    int idx, v, v1; float fv, f1; unsigned uc;
    if (strncmp(line, "db=", 3) == 0) {
        int sh = 0, mx = 20, hd = 1, dp = 2; float scl = 1.0f, x = 0.80f, y = 0.42f, isc = 1.0f, rg = 1.0f;
        const int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d,%d,%f,%f", &sh, &scl, &x, &y, &mx, &hd, &dp, &isc, &rg);
        if (n >= 1) { c.dbShow = sh; if (n >= 2) c.dbScale = scl; if (n >= 3) c.dbX = x; if (n >= 4) c.dbY = y;
                      if (n >= 5) c.dbMax = mx; if (n >= 6) c.dbHeader = hd; if (n >= 7) c.dbDisp = dp;
                      if (n >= 8) c.dbIconScale = isc; if (n >= 9) c.dbRowGap = rg; }
        return true;
    }
    if (sscanf(line, "dbText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < DB_TE_COUNT) {
        TextStyle& ts = c.dbText[idx];
        ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
        ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        return true;
    }
    if (!strncmp(line, "dbbox=", 6)) { parse_box(line + 6, c.dbBox); return true; }
    if (!strncmp(line, "plreqbox=", 9)) { parse_box(line + 9, c.plrEqBox); return true; }   // DETACHED equipment box chrome (out-of-line : same C1061 reason)
    { int mn; if (sscanf(line, "tgtRangeMin=%d", &mn) == 1) { c.tgtRangeMin = mn; return true; } }   // out-of-line : keeps the main chain off MSVC's C1061 limit
    { unsigned cc, cn, cf; if (sscanf(line, "distcol=%x,%x,%x", &cc, &cn, &cf) == 3) { c.distColClose = cc; c.distColNormal = cn; c.distColFar = cf; return true; } }   // distance-zone colours (out-of-line : same C1061 reason)
    return false;
}

// Cast-placeholder lines, parsed OUT-OF-LINE (same reason as parse_ep_line : the else-if chain sits at MSVC's
// C1061 nesting limit -- adding these two branches inline blew it). Consumed via `continue`, zero chain depth.
static bool parse_cast_line(const char* line, UiConfig& c) {
    int v;
    if (sscanf(line, "tgtCastDemo=%d", &v) == 1) { c.tgtCastDemo = v; return true; }
    if (sscanf(line, "plrCastDemo=%d", &v) == 1) { c.plrCastDemo = v; return true; }
    return false;
}

// Minimap EXTRA options (round bezel width / cardinal size / bezel on-off / square border width), parsed
// OUT-OF-LINE (same C1061 nesting reason as parse_ep_line : the main else-if chain is at MSVC's limit).
// Distinct key mm5= ; missing keys keep the code defaults so an OLD config loads to the current look.
// Limbus row toggles (floor-on-gauge / currencies / run total / coffer dots), parsed OUT-OF-LINE for the same
// C1061 reason as parse_mm_line. Missing key keeps the defaults, so an older config loads with every row shown.
static bool parse_zt_line(const char* line, UiConfig& c) {
    if (strncmp(line, "ztlimbus=", 9) == 0) {
        int fl = 1, cu = 1, rn = 1, ch = 1, nm = 1; float bw = 1.0f, bh = 1.0f;
        const int n = sscanf(line + 9, "%d,%d,%d,%d,%d,%f,%f", &fl, &cu, &rn, &ch, &nm, &bw, &bh);
        if (n >= 1) { c.ztLbFloor = fl; if (n >= 2) c.ztLbCur = cu; if (n >= 3) c.ztLbRun = rn; if (n >= 4) c.ztLbChips = ch;
                      if (n >= 5) c.ztLbName = nm; if (n >= 6) c.ztLbBarW = bw; if (n >= 7) c.ztLbBarH = bh; }
        return true;
    }
    if (strncmp(line, "ztdyn=", 6) == 0) {
        int tm = 1, ki = 1; float bw = 1.0f, bh = 1.0f, dt = 1.0f;
        const int n = sscanf(line + 6, "%d,%d,%f,%f,%f", &tm, &ki, &bw, &bh, &dt);
        if (n >= 1) { c.ztDyTimer = tm; if (n >= 2) c.ztDyKi = ki; if (n >= 3) c.ztDyBarW = bw;
                      if (n >= 4) c.ztDyBarH = bh; if (n >= 5) c.ztDyDot = dt; }
        return true;
    }
    if (strncmp(line, "ztaby=", 6) == 0) {
        int tm = 1, lt = 1; float bw = 1.0f, bh = 1.0f, lw = 1.0f, lh = 1.0f;
        const int n = sscanf(line + 6, "%d,%d,%f,%f,%f,%f", &tm, &lt, &bw, &bh, &lw, &lh);
        if (n >= 1) { c.ztAbTimer = tm; if (n >= 2) c.ztAbLights = lt; if (n >= 3) c.ztAbBarW = bw;
                      if (n >= 4) c.ztAbBarH = bh; if (n >= 5) c.ztAbLightW = lw; if (n >= 6) c.ztAbLightH = lh; }
        return true;
    }
    if (strncmp(line, "ztomen=", 7) == 0) {
        int ob = 1, ct = 1, rw = 1;
        const int n = sscanf(line + 7, "%d,%d,%d", &ob, &ct, &rw);
        if (n >= 1) { c.ztOmObj = ob; if (n >= 2) c.ztOmCount = ct; if (n >= 3) c.ztOmRows = rw; }
        return true;
    }
    if (strncmp(line, "ztnyzul=", 8) == 0) {
        int fl = 1, tm = 1, ob = 1, rs = 1, cp = 1, rt = 1, tk = 1;
        const int n = sscanf(line + 8, "%d,%d,%d,%d,%d,%d,%d", &fl, &tm, &ob, &rs, &cp, &rt, &tk);
        if (n >= 1) { c.ztNyFloor = fl; if (n >= 2) c.ztNyTime = tm; if (n >= 3) c.ztNyObj = ob; if (n >= 4) c.ztNyRestr = rs;
                      if (n >= 5) c.ztNyComp = cp; if (n >= 6) c.ztNyRate = rt; if (n >= 7) c.ztNyTok = tk; }
        return true;
    }
    if (strncmp(line, "ztsheol2=", 9) == 0) {
        int fa = 1; float ic = 1.0f, dt = 1.0f;
        const int n = sscanf(line + 9, "%d,%f,%f", &fa, &ic, &dt);
        if (n >= 1) { c.ztShFam = fa; if (n >= 2) c.ztShIcon = ic; if (n >= 3) c.ztShDot = dt; }
        return true;
    }
    return false;
}

static bool parse_mm_line(const char* line, UiConfig& c) {
    if (strncmp(line, "mm5=", 4) == 0) {
        float bw = 1.0f, cs = 1.0f, sb = 1.0f; int bz = 1;
        const int n = sscanf(line + 4, "%f,%f,%d,%f", &bw, &cs, &bz, &sb);
        if (n >= 1) { c.mmBezelW = bw; if (n >= 2) c.mmCardSz = cs; if (n >= 3) c.mmBezel = bz; if (n >= 4) c.mmSqBorder = sb; }
        return true;
    }
    return false;
}

static bool load_config_from(const char* path) {
    CNumLoc _cnl;   // dot decimals regardless of the OS locale
    FILE* f = fopen(path, "r"); if (!f) return false;
    UiConfig& c = ui_config();
    c.guideGroupCount = 0;   // dynamic list -> rebuilt from the file (a full-config load)
    for (int j = 1; j <= 23; ++j) c.tmTrackOffN[j] = 0;   // per-job track blacklists are REBUILT from the file : a job with no tmTrkOff line
                                                          //   means "track everything" -> must not inherit the previous config's keys (else a profile that cleared a job stays stale + re-saves polluted).
    c.tmPreset = 0;          // reflect THIS file's value (fields overlay ; reset so an un-seeded file re-seeds even if a prior config was seeded)
    static char line[8192];  // BIG : a "tmTrkOff<job>=" line can hold up to TM_TRACK_MAX (512) comma-separated keys ~= 3 KB.
                             // A small buffer truncated it mid-line, so most tracked-spell keys were dropped on reload.
    while (fgets(line, sizeof(line), f)) {
        int v, v1, v2, ps, idx, b0, b1, b2, bc; float x, y, s, fv, f1, f2; unsigned uc;
        if (parse_ep_line(line, c)) continue;   // out-of-line : keeps the chain below off MSVC's nesting limit
        if (parse_cast_line(line, c)) continue; // out-of-line : cast-placeholder toggles (same nesting-limit reason)
        if (parse_db_line(line, c)) continue;   // out-of-line : Debuffs module (same nesting-limit reason)
        if (parse_mm_line(line, c)) continue;   // out-of-line : Minimap extra options mm5= (same nesting-limit reason)
        if (parse_zt_line(line, c)) continue;   // out-of-line : Limbus row toggles ztlimbus= (same nesting-limit reason)
        if      (sscanf(line, "partyShow=%d", &v) == 1) c.partyShow = v;
        else if (sscanf(line, "allyShow=%d", &v) == 1)  c.allyShow = v;
        else if (sscanf(line, "tgtShow=%d", &v) == 1)   c.tgtShow = v;
        else if (sscanf(line, "plrShow=%d", &v) == 1)   c.plrShow = v;
        else if (sscanf(line, "skinTheme=%d", &v) == 1) c.skinTheme = v;
        else if (sscanf(line, "skinLum=%f", &fv) == 1)  c.skinLum = fv;
        else if (sscanf(line, "skinHue=%x", &uc) == 1)  c.skinHue = uc;
        else if (sscanf(line, "skinBoxAlpha=%f", &fv) == 1) c.skinBoxAlpha = fv;
        else if (sscanf(line, "allyThemeCopy=%d", &v) == 1) c.allyThemeCopy = v;
        else if (sscanf(line, "allyTheme=%d", &v) == 1) c.allyTheme = v;
        else if (sscanf(line, "allyLum=%f", &fv) == 1) c.allyLum = fv;
        else if (sscanf(line, "allyHue=%x", &uc) == 1) c.allyHue = uc;
        else if (sscanf(line, "allyBoxAlpha=%f", &fv) == 1) c.allyBoxAlpha = fv;
        else if (sscanf(line, "fontFace=%d", &v) == 1)  c.fontFace = v;
        else if (sscanf(line, "buffScale=%f", &fv) == 1) c.buffScale = fv;
        else if (sscanf(line, "buffMax=%d", &v) == 1)    c.buffMax = v;
        else if (sscanf(line, "buffRows=%d", &v) == 1)   c.buffRows = v;
        else if (sscanf(line, "uiStyle=%d", &v) == 1)    c.uiStyle = v;
        else if (sscanf(line, "uiColor=%d", &v) == 1)    c.uiColor = v;
        else if (sscanf(line, "uiAccent=%x", &uc) == 1)  c.uiAccent = uc;
        else if (sscanf(line, "uiCursor=%d", &v) == 1)   c.uiCursor = v;
        else if (sscanf(line, "hidePeekMode=%d", &v) == 1) c.hidePeekMode = v;
        else if (sscanf(line, "cursorScale=%f", &fv) == 1) c.cursorScale = fv;
        else if (sscanf(line, "tgtBox=%d", &v) == 1)     c.tgtBox = v;
        else if (sscanf(line, "tgtBoxAlpha=%f", &fv) == 1) c.tgtBoxAlpha = fv;
        else if (sscanf(line, "tgtNameHostile=%d", &v) == 1) c.tgtNameHostile = v;
        else if (sscanf(line, "tgtSpeedIcon=%d", &v) == 1) c.tgtSpeedIcon = v;
        else if (sscanf(line, "tgtSpeed=%d", &v) == 1)   c.tgtSpeed = v;
        else if (sscanf(line, "tgtThIcon=%d", &v) == 1)  c.tgtThIcon = v;
        else if (sscanf(line, "tgtTH=%d", &v) == 1)      c.tgtTH = v;
        else if (sscanf(line, "tgtRange=%d", &v) == 1)   c.tgtRange = v;
        else if (sscanf(line, "tgtCast=%d", &v) == 1)    c.tgtCast = v;
        else if (sscanf(line, "tgtSubPos=%d", &v) == 1)  c.tgtSubPos = v;
        else if (sscanf(line, "tgtSub=%d", &v) == 1)     c.tgtSub = v;
        else if (sscanf(line, "plrCast=%d", &v) == 1)    c.plrCast = v;
        else if (sscanf(line, "tgtBuffMax=%d", &v) == 1) c.tgtBuffMax = v;
        else if (sscanf(line, "tgtThemeCopy=%d", &v) == 1) c.tgtThemeCopy = v;
        else if (sscanf(line, "tgtTheme=%d", &v) == 1)   c.tgtTheme = v;
        else if (sscanf(line, "tgtHue=%x", &uc) == 1)    c.tgtHue = uc;
        else if (sscanf(line, "plrHue=%x", &uc) == 1)    c.plrHue = uc;
        else if (sscanf(line, "tgtLum=%f", &fv) == 1)    c.tgtLum = fv;
        else if (sscanf(line, "tgtScale=%f", &fv) == 1)  c.tgtScale = fv;
        else if (sscanf(line, "tgtDebuffs=%d", &v) == 1) c.tgtDebuffs = v;
        else if (sscanf(line, "tgtBuffPos=%d", &v) == 1) c.tgtBuffPos = v;
        else if (sscanf(line, "tgtTimers=%d", &v) == 1)  c.tgtTimers = v;
        else if (sscanf(line, "tgtText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < TGT_TE_COUNT) {
            TextStyle& ts = c.tgtText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (sscanf(line, "tgtBars=%f,%f,%f", &fv, &f1, &f2) == 3) { c.tgtBarH = fv; c.tgtBarW = f1; c.tgtIconSz = f2; }
        else if (sscanf(line, "tgtDetailIconSz=%f", &fv) == 1) c.tgtDetailIconSz = fv;
        else if (sscanf(line, "tgtRangeH=%f", &fv) == 1) c.tgtRangeH = fv;
        else if (sscanf(line, "tgtPos=%d,%f,%f", &v, &fv, &f1) == 3) { c.tgtPosSet = (v != 0); c.tgtX = fv; c.tgtY = f1; }
        else if (sscanf(line, "tgtCenter=%d,%d", &v, &v1) == 2) { c.tgtCenterH = v; c.tgtCenterV = v1; }
        else if (strncmp(line, "plrBox=", 7) == 0) { int a[12] = {0}; a[9] = 1; a[10] = 1; a[11] = 1; int n = sscanf(line + 7, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8],&a[9],&a[10],&a[11]); if (n >= 9) { c.plrBox=a[0]; c.plrEmblem=a[1]; c.plrName=a[2]; c.plrLvl=a[3]; c.plrHp=a[4]; c.plrMp=a[5]; c.plrTp=a[6]; c.plrBuffs=a[7]; c.plrBuffMax=a[8]; c.plrSpeed=a[9]; c.plrGil=a[10]; c.plrEquip=a[11]; } }   // n>=9 : plrSpeed/plrGil/plrEquip (a[9..11], default 1) optional for older files
        else if (strncmp(line, "plrEq=", 6) == 0) { float ce = 1.0f; int tb = 1, pl = 0, bgc = 0; unsigned col = 0xFF6699BBu, bg = 0xE0121620u; int n = sscanf(line + 6, "%f,%d,%x,%d,%d,%x", &ce, &tb, &col, &pl, &bgc, &bg); if (n >= 1) { c.plrEqCell = ce; c.plrEqThemeBorder = tb; c.plrEqColor = col; if (n >= 4) c.plrEqPlace = pl; if (n >= 6) { c.plrEqCellBgCustom = bgc; c.plrEqCellBg = bg; } } }
        else if (strncmp(line, "plrEqDet=", 9) == 0) { int de = 0, ps = 0, gp = 0; float ex = 0.0f, ey = 0.0f, es = 1.0f; int n = sscanf(line + 9, "%d,%d,%f,%f,%f,%d", &de, &ps, &ex, &ey, &es, &gp); if (n >= 1) { c.plrEquipDetach = de; c.plrEquipPosSet = (ps != 0); c.plrEquipX = ex; c.plrEquipY = ey; if (n >= 5) c.plrEquipScale = es; if (n >= 6) c.plrEqGilPlace = gp; } }
        else if (strncmp(line, "mm3=", 4) == 0) { int ck = 1, ti = 1, dy = 1, mo = 1, re = 1, cp = 0; float ms = 1.0f; int n = sscanf(line + 4, "%d,%d,%d,%d,%d,%f,%d", &ck, &ti, &dy, &mo, &re, &ms, &cp); if (n >= 1) { c.mmClock = ck; c.mmClkTime = ti; c.mmClkDay = dy; c.mmClkMoon = mo; c.mmClkReal = re; if (n >= 6) c.mmMapSize = ms; if (n >= 7) c.mmClockPos = cp; } }
        else if (strncmp(line, "wscol=", 6) == 0) { unsigned na = 0, d1 = 0, d2 = 0; if (sscanf(line + 6, "%x,%x,%x", &na, &d1, &d2) == 3) { c.wsNameCol = na; c.wsDmgCol1 = d1; c.wsDmgCol2 = d2; } }
        else if (strncmp(line, "ws=", 3) == 0) { int sh = 1, ft = 0, fx = 1; float sc = 1.0f, x = 0.5f, y = 0.36f; int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d", &sh, &sc, &x, &y, &ft, &fx); if (n >= 1) { c.wsShow = sh; if (n >= 2) c.wsScale = sc; if (n >= 3) c.wsX = x; if (n >= 4) c.wsY = y; if (n >= 5) c.wsFont = ft; if (n >= 6) c.wsFx = fx; } }
        else if (strncmp(line, "sc2=", 4) == 0) { int tm = 1, st = 1, pr = 1, ls = 1, ti = 1; float lg = 1.0f; int n = sscanf(line + 4, "%d,%d,%d,%d,%d,%f", &tm, &st, &pr, &ls, &ti, &lg); if (n >= 1) { c.scTimer = tm; c.scStep = st; c.scProps = pr; c.scList = ls; if (n >= 5) c.scTitle = ti; if (n >= 6) c.scListGap = lg; } }
        else if (sscanf(line, "scText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < SC_TE_COUNT) {
            TextStyle& ts = c.scText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (strncmp(line, "sc=", 3) == 0) { int sh = 1; float scl = 1.0f, x = 0.78f, y = 0.06f; int nb = 1; int n = sscanf(line + 3, "%d,%f,%f,%f,%d", &sh, &scl, &x, &y, &nb); if (n >= 1) { c.scShow = sh; if (n >= 2) c.scScale = scl; if (n >= 3) c.scX = x; if (n >= 4) c.scY = y; if (n >= 5) c.scNearby = nb; } }
        else if (strncmp(line, "tp=", 3) == 0) { int sh = 1, cnt = 10, ic = 1; float scl = 1.0f, x = 0.72f, y = 0.30f; int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d", &sh, &scl, &x, &y, &cnt, &ic); if (n >= 1) { c.tpShow = sh; if (n >= 2) c.tpScale = scl; if (n >= 3) c.tpX = x; if (n >= 4) c.tpY = y; if (n >= 5) c.tpCount = cnt; if (n >= 6) c.tpIcon = ic; } }
        else if (sscanf(line, "tpText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < TP_TE_COUNT) {
            TextStyle& ts = c.tpText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (strncmp(line, "hl=", 3) == 0) { int sh = 1, cnt = 8, sd = 1, st = 1; float scl = 1.0f, x = 0.22f, y = 0.32f; int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d,%d", &sh, &scl, &x, &y, &cnt, &sd, &st); if (n >= 1) { c.hlShow = sh; if (n >= 2) c.hlScale = scl; if (n >= 3) c.hlX = x; if (n >= 4) c.hlY = y; if (n >= 5) c.hlCount = cnt; if (n >= 6) c.hlDist = sd; if (n >= 7) c.hlTgt = st; } }
        else if (sscanf(line, "hlText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < HL_TE_COUNT) {
            TextStyle& ts = c.hlText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (strncmp(line, "pw=", 3) == 0) { int sh = 1, md = 0, rt = 1, ly = 0, dp = 0; float scl = 1.0f, x = 0.015f, y = 0.04f; int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d,%d,%d", &sh, &scl, &x, &y, &md, &rt, &ly, &dp); if (n >= 1) { c.pwShow = sh; if (n >= 2) c.pwScale = scl; if (n >= 3) c.pwX = x; if (n >= 4) c.pwY = y; if (n >= 5) c.pwMode = md; if (n >= 6) c.pwRate = rt; if (n >= 7) c.pwLayout = ly; if (n >= 8) c.pwDisplay = dp; } }
        else if (sscanf(line, "pwText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < PW_TE_COUNT) {
            TextStyle& ts = c.pwText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (strncmp(line, "grim=", 5) == 0) { int sh = 1, ar = 2; float scl = 1.0f, x = 0.28f, y = 0.58f; int n = sscanf(line + 5, "%d,%f,%f,%f,%d", &sh, &scl, &x, &y, &ar); if (n >= 1) { c.grimShow = sh; if (n >= 2) c.grimScale = scl; if (n >= 3) c.grimX = x; if (n >= 4) c.grimY = y; if (n >= 5) c.grimArt = ar; } }
        else if (sscanf(line, "grimText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < GRIM_TE_COUNT) {
            TextStyle& ts = c.grimText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (strncmp(line, "zt=", 3) == 0) { int sh = 1, vr = 1, hd = 1; float scl = 1.0f, x = 0.145f, y = 0.33f; int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d", &sh, &scl, &x, &y, &vr, &hd); if (n >= 1) { c.ztShow = sh; if (n >= 2) c.ztScale = scl; if (n >= 3) c.ztX = x; if (n >= 4) c.ztY = y; if (n >= 5) c.ztVariant = vr; if (n >= 6) c.ztHeader = hd; } }
        else if (strncmp(line, "ztsheol=", 8) == 0) { int sg = 1, rs = 1, jk = 1; int n = sscanf(line + 8, "%d,%d,%d", &sg, &rs, &jk); if (n >= 1) { c.ztSheolSeg = sg; if (n >= 2) c.ztSheolRes = rs; if (n >= 3) c.ztSheolJoke = jk; } }
        else if (strncmp(line, "tm=", 3) == 0) { int sh = 1, mx = 16, ti = 1, bx = 1, mg = 1, dm = 0, rm = 0, ot = 1, mn = 1, bs = -1, sp = 1; float scl = 1.0f, x = 0.86f, y = 0.30f, rx = 0.86f, ry2 = 0.44f, isc = 1.0f, rg = 1.0f; int n = sscanf(line + 3, "%d,%f,%f,%f,%d,%d,%d,%d,%f,%f,%d,%d,%d,%f,%d,%d,%d,%f", &sh, &scl, &x, &y, &mx, &ti, &bx, &mg, &rx, &ry2, &dm, &rm, &ot, &isc, &mn, &bs, &sp, &rg); if (n >= 1) { c.tmShow = sh; if (n >= 2) c.tmScale = scl; if (n >= 3) c.tmX = x; if (n >= 4) c.tmY = y; if (n >= 5) c.tmMax = mx; if (n >= 6) c.tmTitle = ti; if (n >= 7) c.tmBox.on = bx; if (n >= 8) c.tmMerged = mg; if (n >= 9) c.tmRX = rx; if (n >= 10) c.tmRY = ry2; if (n >= 11) c.tmDurMode = dm; if (n >= 12) c.tmRecMode = rm; if (n >= 13) c.tmOthers = ot; if (n >= 14) c.tmIconScale = isc; if (n >= 15) c.tmMine = mn; c.tmBuffSrc = (n >= 16 && bs >= 0) ? bs : (ot ? 3 : 0); if (n >= 17) c.tmSpAlert = sp; if (n >= 18) c.tmRowGap = rg; } }   // tmBuffSrc absent (old config) -> migrate from tmOthers
        else if (sscanf(line, "ztText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < ZT_TE_COUNT) {
            TextStyle& ts = c.ztText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (sscanf(line, "tmText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < TM_TE_COUNT) {
            TextStyle& ts = c.tmText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (sscanf(line, "tmAllyGroup=%d", &v) == 1) c.tmAllyGroup = v;
        else if (sscanf(line, "tmFocus=%d,%d", &v, &v1) == 2) { c.tmFocusWarn = v; c.tmFocusHold = v1; }
        else if (sscanf(line, "tmPreset=%d", &v) == 1) c.tmPreset = v;
        else if (!strncmp(line, "tmTrkOff", 8)) {                                 // Timers "track per job" : disabled keys
            int j = atoi(line + 8); const char* p = strchr(line, '=');
            if (p && j >= 1 && j <= 23) {
                c.tmTrackOffN[j] = 0;
                for (++p; *p && c.tmTrackOffN[j] < UiConfig::TM_TRACK_MAX; ) {
                    while (*p == ',' || *p == ' ') ++p;
                    if (*p < '0' || *p > '9') break;
                    unsigned k = (unsigned)strtoul(p, (char**)&p, 10);
                    if (k) c.tmTrackOff[j][c.tmTrackOffN[j]++] = (unsigned short)k;
                }
            }
        }
        else if (!strncmp(line, "scbox=", 6)) parse_box(line + 6, c.scBox);
        else if (!strncmp(line, "tpbox=", 6)) parse_box(line + 6, c.tpBox);
        else if (!strncmp(line, "hlbox=", 6)) parse_box(line + 6, c.hlBox);
        else if (!strncmp(line, "pwbox=", 6)) parse_box(line + 6, c.pwBox);
        else if (!strncmp(line, "ztbox=", 6)) parse_box(line + 6, c.ztBox);
        else if (!strncmp(line, "tmbox=", 6)) parse_box(line + 6, c.tmBox);
        else if (!strncmp(line, "mmbox=", 6)) parse_box(line + 6, c.mmBox);
        else if (strncmp(line, "mm2=", 4) == 0) { int sp = 0, fr = 1, pc = 1, np = 1, mb = 1; unsigned fc = 0xFF6699BBu; float ba = 0.0f, msc = 1.0f; if (sscanf(line + 4, "%d,%d,%x,%f,%f,%d,%d,%d", &sp, &fr, &fc, &ba, &msc, &pc, &np, &mb) >= 1) { c.mmShape = sp; c.mmFrame = fr; c.mmFrameColor = fc; c.mmBgAlpha = ba; c.mmMarkerScale = msc; c.mmPC = pc; c.mmNPC = np; c.mmMob = mb; } }
        else if (strncmp(line, "mm4=", 4) == 0) { int tl = 1, rg = 0; unsigned tc = 0xFFFF6A6Au, rc = 0xFF66E0FFu; float rr = 20.0f; if (sscanf(line + 4, "%d,%x,%d,%f,%x", &tl, &tc, &rg, &rr, &rc) >= 1) { c.mmTgtLine = tl; c.mmTgtLineCol = tc; c.mmRing = rg; c.mmRingR = rr; c.mmRingCol = rc; } }   // target-line/ring (renamed from mm3= : it collided with the clock's mm3= and never loaded)
        else if (strncmp(line, "mm=", 3) == 0) { int sh = 1, ps = 0; float mx = 0.0f, my = 0.0f, ms = 1.0f, mz = 2.0f; int n = sscanf(line + 3, "%d,%d,%f,%f,%f,%f", &sh, &ps, &mx, &my, &ms, &mz); if (n >= 1) { c.mmShow = sh; c.mmPosSet = ps != 0; c.mmX = mx; c.mmY = my; if (n >= 5) c.mmScale = ms; if (n >= 6) c.mmZoom = mz; } }
        else if (sscanf(line, "plrEmblemSz=%f", &fv) == 1) c.plrEmblemSz = fv;
        else if (sscanf(line, "plrBarGap=%f", &fv) == 1)   c.plrBarGap = fv;
        else if (sscanf(line, "plrText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < PLR_TE_COUNT) {
            TextStyle& ts = c.plrText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (sscanf(line, "mmText%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < MM_TE_COUNT) {
            TextStyle& ts = c.mmText[idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (strncmp(line, "plrSizes=", 9) == 0) { float s[5] = {0}; if (sscanf(line + 9, "%f,%f,%f,%f,%f", &s[0],&s[1],&s[2],&s[3],&s[4]) == 5) { c.plrBoxAlpha=s[0]; c.plrScale=s[1]; c.plrBarH=s[2]; c.plrBarW=s[3]; c.plrIconSz=s[4]; } }
        else if (sscanf(line, "plrTheme=%d,%d", &v, &v1) == 2) { c.plrThemeCopy = v; c.plrTheme = v1; }
        else if (sscanf(line, "plrLum=%f", &fv) == 1)    c.plrLum = fv;
        else if (sscanf(line, "plrPos=%d,%f,%f", &v, &fv, &f1) == 3) { c.plrPosSet = (v != 0); c.plrX = fv; c.plrY = f1; }
        else if (sscanf(line, "plrCenter=%d,%d", &v, &v1) == 2) { c.plrCenterH = v; c.plrCenterV = v1; }
        else if (sscanf(line, "barHeight=%f,%f,%f", &fv, &f1, &f2) == 3) { c.barHeight[0] = fv; c.barHeight[1] = f1; c.barHeight[2] = f2; }
        else if (sscanf(line, "barHeight=%f", &fv) == 1) { c.barHeight[0] = c.barHeight[1] = c.barHeight[2] = fv; }   // old single value -> all boxes
        else if (sscanf(line, "barWidth=%f,%f,%f", &fv, &f1, &f2) == 3) { c.barWidth[0] = fv; c.barWidth[1] = f1; c.barWidth[2] = f2; }
        else if (sscanf(line, "barWidth=%f", &fv) == 1) { c.barWidth[0] = c.barWidth[1] = c.barWidth[2] = fv; }
        else if (sscanf(line, "badgeScale=%f,%f,%f", &fv, &f1, &f2) == 3) { c.badgeScale[0] = fv; c.badgeScale[1] = f1; c.badgeScale[2] = f2; }
        else if (sscanf(line, "gaugeStyle=%d,%d,%d", &v, &v1, &v2) == 3) { c.gaugeStyle[0] = v; c.gaugeStyle[1] = v1; c.gaugeStyle[2] = v2; }
        else if (sscanf(line, "gaugeStyle=%d", &v) == 1) { c.gaugeStyle[0] = c.gaugeStyle[1] = c.gaugeStyle[2] = v; }
        else if (sscanf(line, "jobBadge=%d,%d,%d", &v, &v1, &v2) == 3) { c.jobBadge[0] = v; c.jobBadge[1] = v1; c.jobBadge[2] = v2; }
        else if (sscanf(line, "jobBadge=%d", &v) == 1) { c.jobBadge[0] = c.jobBadge[1] = c.jobBadge[2] = v; }
        else if (sscanf(line, "cast=%d,%d,%d", &b0, &b1, &b2) == 3) { c.cast[0] = (b0 != 0); c.cast[1] = (b1 != 0); c.cast[2] = (b2 != 0); }
        else if (sscanf(line, "casts=%d,%d", &b0, &b1) == 2) { c.cast[0] = (b0 != 0); c.cast[1] = c.cast[2] = (b1 != 0); }   // old party,ally -> ally applies to both
        else if (sscanf(line, "dist=%d,%d,%d", &b0, &b1, &b2) == 3) { c.dist[0] = (b0 != 0); c.dist[1] = (b1 != 0); c.dist[2] = (b2 != 0); }
        else if (sscanf(line, "lang=%d", &v) == 1) c.lang = v;
        else if (strncmp(line, "partyRef=", 9) == 0) {   // 3 (old) or 6 values -> fill as many as present
            float pr[6]; int got = sscanf(line + 9, "%f,%f,%f,%f,%f,%f", &pr[0], &pr[1], &pr[2], &pr[3], &pr[4], &pr[5]);
            for (int k = 0; k < got && k < 6; ++k) c.partyRef[k] = pr[k];
        }
        else if (sscanf(line, "partyBottom=%f", &fv) == 1) c.partyBottomY = fv;
        else if (sscanf(line, "partyRefX=%f,%f", &x, &y) == 2) { c.partyRefX[0] = x; c.partyRefX[1] = y; }
        else if (strncmp(line, "allyRef=", 8) == 0) { float ar[4]; int g = sscanf(line + 8, "%f,%f,%f,%f", &ar[0], &ar[1], &ar[2], &ar[3]); for (int k = 0; k < g && k < 4; ++k) c.allyRefY[k] = ar[k]; }
        else if (sscanf(line, "zonePanel=%f,%f", &x, &y) == 2) { c.zonePanelX = x; c.zonePanelY = y; }
        else if (strncmp(line, "zone=", 5) == 0 && c.guideGroupCount < GUIDE_GROUPS_MAX) {
            GuideGroup z; char nm[24] = { 0 }; float x = 0, y = 0, w = 0, h = 0; int a0 = 0, a1 = 0, a2 = 0, rl = 0, off = 0;
            // parse the 7 numeric fields common to BOTH formats ; the tail is either "role,name" (new) or
            // "name" (legacy). Detect the optional role by an integer FOLLOWED BY a comma, so a legacy name
            // that starts with a digit (e.g. "2nd wall") is no longer misread as a role field.
            if (sscanf(line + 5, "%f,%f,%f,%f,%d,%d,%d%n", &x, &y, &w, &h, &a0, &a1, &a2, &off) == 7) {
                const char* rest = line + 5 + off; if (*rest == ',') ++rest;    // skip the separating comma
                const char* d = rest; if (*d == '-') ++d;
                const char* e = d; while (*e >= '0' && *e <= '9') ++e;
                if (e > d && *e == ',') {                                        // "<int>," -> a real role field
                    int sgn = 1; const char* q = rest; if (*q == '-') { sgn = -1; ++q; }
                    while (q < e) { rl = rl * 10 + (*q - '0'); ++q; }
                    rl *= sgn; rest = e + 1;
                }
                int k = 0; for (; k < 23 && rest[k] && rest[k] != '\r' && rest[k] != '\n'; ++k) nm[k] = rest[k]; nm[k] = 0;
                z.x = x; z.y = y; z.w = w; z.h = h; z.allow[0] = (a0 != 0); z.allow[1] = (a1 != 0); z.allow[2] = (a2 != 0);
                z.allow[ZPERM_TARGET] = (rl & 0x100) != 0; z.role = rl & 0xFF;   // unpack the Target-allow bit from role
                strncpy(z.name, nm, sizeof(z.name) - 1); z.name[sizeof(z.name) - 1] = 0;
                c.guideGroup[c.guideGroupCount++] = z;
            }
        }
        else if (sscanf(line, "border=%d,%d,%d,%d", &b0, &b1, &b2, &bc) == 4) {
            c.border[0] = (b0 != 0); c.border[1] = (b1 != 0); c.border[2] = (b2 != 0); c.borderCost = (bc != 0);
        }
        else if (sscanf(line, "anim=%d,%d", &b0, &b1) == 2) { c.animHP = (b0 != 0); c.animTP = (b1 != 0); }
        else if (sscanf(line, "textP%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < TE_COUNT) {
            TextStyle& ts = c.text[0][idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (sscanf(line, "textA%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < TE_COUNT) {
            TextStyle& ts = c.text[1][idx];
            ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
            ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
        }
        else if (sscanf(line, "text%d=%d,%f,%f,%d,%x", &idx, &v, &fv, &f1, &v1, &uc) == 6 && idx >= 0 && idx < TE_COUNT) {
            for (int g = 0; g < 2; ++g) {   // legacy (pre per-box typography) : apply to BOTH groups
                TextStyle& ts = c.text[g][idx];
                ts.face = v; ts.size = fv; ts.outline = f1; ts.color = uc;
                ts.bold = (v1 & 1) != 0; ts.italic = (v1 & 2) != 0; ts.upper = (v1 & 4) != 0; ts.colorOn = (v1 & 8) != 0;
            }
        }
        else if (sscanf(line, "box%d=%d,%f,%f,%f", &idx, &ps, &x, &y, &s) == 5 && idx >= 0 && idx < 3) {
            // sanitise : a corrupt position (out of the [0,1] screen fraction) must never brick the box
            // off-screen where it can't be grabbed back. Clamp the placed top-left to the viewport.
            x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
            y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
            c.box[idx].posSet = (ps != 0); c.box[idx].x = x; c.box[idx].y = y; c.box[idx].scale = s;
        }
    }
    fclose(f);
    // sanitise numeric loads : a hand-edited / corrupt file must never feed out-of-range multipliers or
    // counts into the draw loops (the config UI already enforces these ranges ; this is pure defense).
    #define CLF(x, lo, hi) do { if (x < (lo)) x = (lo); else if (x > (hi)) x = (hi); } while (0)
    CLF(c.buffScale, 0.10f, 4.0f); CLF(c.cursorScale, 0.10f, 4.0f);
    if (c.buffMax < 0) c.buffMax = 0; else if (c.buffMax > 32) c.buffMax = 32;   // 0 = no party/alliance buffs
    if (c.buffRows < 1) c.buffRows = 1; else if (c.buffRows > 2) c.buffRows = 2;
    if (c.tmFocusWarn < 10) c.tmFocusWarn = 10; else if (c.tmFocusWarn > 300) c.tmFocusWarn = 300;
    if (c.tmFocusHold < 5)  c.tmFocusHold = 5;  else if (c.tmFocusHold > 300) c.tmFocusHold = 300;
    if (c.uiStyle < 0) c.uiStyle = 0; else if (c.uiStyle > 15) c.uiStyle = 15;
    if (c.uiColor < 0) c.uiColor = 0; else if (c.uiColor > 35) c.uiColor = 35;   // 12 hues x 3 lightness rows = 36 swatches (0..35)
    CLF(c.skinLum, -1.0f, 1.0f);
    CLF(c.skinBoxAlpha, 0.0f, 1.0f);
    for (int k = 0; k < 3; ++k) {
        CLF(c.barHeight[k], 0.10f, 4.0f); CLF(c.barWidth[k], 0.10f, 4.0f); CLF(c.badgeScale[k], 0.10f, 4.0f);
        if (c.gaugeStyle[k] < 0 || c.gaugeStyle[k] > 7) c.gaugeStyle[k] = 0;
        if (c.jobBadge[k]   < 0 || c.jobBadge[k]   > 3) c.jobBadge[k]   = 2;
    }
    #undef CLF
    if (c.tmPreset < 1) { apply_rdm_uff_preset(c); c.tmPreset = 1; }   // seed the RDM default once per file
    return true;
}

static bool g_editShift = false, g_editCtrl = false, g_editAlt = false;
void edit_set_modifiers(bool shift, bool ctrl, bool alt) { g_editShift = shift; g_editCtrl = ctrl; g_editAlt = alt; }
bool edit_shift() { return g_editShift; }
bool edit_ctrl()  { return g_editCtrl; }
bool edit_alt()   { return g_editAlt; }

void save_ui_config() { save_config_to(config_path()); }

// ---- one-time migration : loose files in the plugin ROOT -> a tidy data\ subfolder (config / profiles / cache) ----
// Idempotent : only moves a file when the destination doesn't already exist. Runs once at startup (before any read),
// so an existing install keeps every setting/profile ; a fresh install just creates the empty dirs.
static void move_if(const char* oldRel, const char* newRel) {
    char oldp[300], newp[300]; plugin_path(oldp, sizeof(oldp), oldRel); plugin_path(newp, sizeof(newp), newRel);
    if (GetFileAttributesA(newp) != INVALID_FILE_ATTRIBUTES) return;   // destination already there -> never overwrite
    if (GetFileAttributesA(oldp) == INVALID_FILE_ATTRIBUTES) return;   // source absent -> nothing to move
    MoveFileA(oldp, newp);
}
// Move every file matching `patternRel` (a plugin-relative wildcard) from `srcDirRel` into `destDirRel`.
// If `decodeSlash`, a "%2F" in the source name becomes "-" (migrates the old profile separator).
static void move_glob(const char* patternRel, const char* srcDirRel, const char* destDirRel, bool decodeSlash) {
    char pat[300]; plugin_path(pat, sizeof(pat), patternRel);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char nn[160]; int o = 0; const char* s = fd.cFileName;
        for (int i = 0; s[i] && o < 156; ) {
            if (decodeSlash && s[i] == '%' && s[i + 1] == '2' && (s[i + 2] == 'F' || s[i + 2] == 'f')) { nn[o++] = '-'; i += 3; }
            else nn[o++] = s[i++];
        }
        nn[o] = 0;
        char oR[280], nR[280];
        if (srcDirRel[0]) _snprintf(oR, sizeof(oR), "%s\\%s", srcDirRel, fd.cFileName);
        else              _snprintf(oR, sizeof(oR), "%s", fd.cFileName);
        _snprintf(nR, sizeof(nR), "%s\\%s", destDirRel, nn);
        oR[sizeof(oR) - 1] = 0; nR[sizeof(nR) - 1] = 0;
        move_if(oR, nR);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static void migrate_data_folder() {
    static bool done = false; if (done) return; done = true;
    char p[300];
    plugin_path(p, sizeof(p), "data");            CreateDirectoryA(p, NULL);   // parents first (CreateDirectory won't make intermediates)
    plugin_path(p, sizeof(p), "data\\profiles");  CreateDirectoryA(p, NULL);
    plugin_path(p, sizeof(p), "data\\cache");     CreateDirectoryA(p, NULL);
    move_if("aio_config.txt",       "data\\config.txt");
    move_if("aio_active.txt",       "data\\active.txt");
    move_if("aio_charprofiles.txt", "data\\charprofiles.txt");
    move_if("aiohud_party.bin",     "data\\cache\\party.bin");
    move_if("aiohud_zone.bin",      "data\\cache\\zone.bin");
    move_glob("state_*.bin",            "",                "data\\cache",    false);   // per-zone derived-state caches
    move_glob("aiohud_profiles\\*.txt", "aiohud_profiles", "data\\profiles", true);    // profiles (+ "%2F" -> "-")
    plugin_path(p, sizeof(p), "aiohud_profiles"); RemoveDirectoryA(p);   // drop the now-empty legacy dir (no-op if not empty)
}

// FRESH INSTALL : if there is no config yet, seed the shipped "Default" profile (assets\default_profile.txt) so a
// first launch comes up with a curated setup instead of the bare code defaults -- it appears in the profile library,
// becomes the live config, and is marked active. Never touches an existing install (config.txt already present).
static void seed_default_profile() {
    char cfg[300]; plugin_path(cfg, sizeof(cfg), "data\\config.txt");
    if (GetFileAttributesA(cfg) != INVALID_FILE_ATTRIBUTES) return;         // already configured -> leave everything as-is
    char def[300]; plugin_path(def, sizeof(def), "assets\\default_profile.txt");
    if (GetFileAttributesA(def) == INVALID_FILE_ATTRIBUTES) return;         // no shipped default -> fall back to code defaults
    char prof[300]; plugin_path(prof, sizeof(prof), "data\\profiles\\Default.txt");
    CopyFileA(def, prof, TRUE);   // "Default" shows up in the profile library
    CopyFileA(def, cfg, TRUE);    // adopt it as the live config
    FILE* f = fopen(active_path(), "w"); if (f) { fputs("Default", f); fclose(f); }   // mark it the active profile
}

void load_ui_config() {
    migrate_data_folder();   // move any legacy root files into data\ BEFORE the first read
    seed_default_profile();  // first-run only : plant the shipped Default profile as the starting config
    { char vp[300]; plugin_path(vp, sizeof(vp), "data\\version.txt"); FILE* vf = fopen(vp, "w"); if (vf) { fputs(AIOHUD_VERSION, vf); fclose(vf); } }   // expose the build version for the companion updater
    load_config_from(config_path());
    // remember + AUTO-APPLY the last loaded profile so a relaunch comes back on the same profile.
    FILE* f = fopen(active_path(), "r");
    if (f) {
        if (fgets(g_active, sizeof(g_active), f)) {
            size_t n = strlen(g_active);
            while (n && (g_active[n-1] == '\n' || g_active[n-1] == '\r' || g_active[n-1] == ' ')) g_active[--n] = 0;
        }
        fclose(f);
    }
    if (g_active[0]) {
        char p[300]; profile_path(g_active, p, sizeof(p));
        if (load_config_from(p)) save_config_to(config_path());   // adopt the profile as the live config
        else g_active[0] = 0;                                   // profile was deleted -> no active profile
    }
    profile_mark_clean();
    request_scale_baseline_reset();   // startup : adopt the loaded scales as baseline (no re-anchor)
}

// ---- profiles ----
// The display NAME and the FILE name are decoupled : the user can type any printable character
// (incl. / \ : etc.), and we %-encode the filename-illegal ones into the file name (reversible, no
// collisions). So "PlayerName1/Default" lives in "PlayerName1%2FDefault.txt" and shows back as typed.
static char g_profNames[64][48];   // DISPLAY names (decoded)
static int  g_profCount = 0;

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
// display name -> file base name (no extension). %-encode the chars Windows forbids in a file name.
static void name_to_file(const char* name, char* out, int cap) {
    int o = 0;
    for (int i = 0; name[i] && o < cap - 4; ++i) {
        unsigned char ch = (unsigned char)name[i];
        bool bad = ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' ||
                   ch == '\\' || ch == '|' || ch == '?' || ch == '*' || ch == '%';
        if (bad) { o += sprintf(out + o, "%%%02X", ch); }
        else out[o++] = (char)ch;
    }
    out[o] = 0;
}
// file base name -> display name (%XX -> byte).
static void file_to_name(const char* file, char* out, int cap) {
    int o = 0;
    for (int i = 0; file[i] && o < cap - 1; ++i) {
        if (file[i] == '%' && file[i + 1] && file[i + 2]) {
            int hi = hexval(file[i + 1]), lo = hexval(file[i + 2]);
            if (hi >= 0 && lo >= 0) { out[o++] = (char)(hi * 16 + lo); i += 2; continue; }
        }
        out[o++] = file[i];
    }
    out[o] = 0;
}

static void profile_path(const char* name, char* out, int cap) {
    char fb[160]; name_to_file(name, fb, sizeof(fb));
    _snprintf(out, cap, "%s\\%s.txt", profile_dir(), fb); out[cap - 1] = 0;
}
void profile_refresh() {
    g_profCount = 0;
    char pat[300]; _snprintf(pat, sizeof(pat), "%s\\*.txt", profile_dir()); pat[sizeof(pat) - 1] = 0;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char base[160]; int n = (int)strlen(fd.cFileName);
        if (n > 4 && _stricmp(fd.cFileName + n - 4, ".txt") == 0) n -= 4;   // strip extension
        if (n <= 0) continue; if (n > 159) n = 159;
        memcpy(base, fd.cFileName, n); base[n] = 0;
        if (g_profCount < 64) { file_to_name(base, g_profNames[g_profCount], 48); ++g_profCount; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
int         profile_count()       { return g_profCount; }
const char* profile_name(int i)   { return (i >= 0 && i < g_profCount) ? g_profNames[i] : ""; }
bool        profile_exists(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}
bool profile_save(const char* name) {
    if (!name || !name[0]) return false;
    CreateDirectoryA(profile_dir(), NULL);
    char p[300]; profile_path(name, p, sizeof(p));
    save_config_to(p);
    profile_refresh();
    profile_mark_clean();
    set_active_profile(name);   // saving makes it the active profile too
    return true;
}
bool profile_load(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    if (!load_config_from(p)) return false;
    save_ui_config();   // adopt as the live config too
    profile_mark_clean();
    set_active_profile(name);   // remember for auto-load at next startup
    request_scale_baseline_reset();   // adopt the profile's box scales as-is (don't re-anchor -> no false "modified")
    return true;
}

// Called every frame : when the character's Name/Main/Sub changes (login, job change), auto-switch profiles.
// Priority : (1) a profile named exactly "Name Main/Sub" ; (2) else the last profile THIS character loaded manually.
// A custom-named profile ("PlayerName1 WAR Odyssey Bumba") never matches (1), so it stays manual — until you load it,
// after which it becomes this character's (2) fallback. Does nothing (keeps the current config) if neither exists.
void profile_autoload_tick() {
    char combo[64]; if (!profile_default_name(combo, sizeof(combo))) return;   // character not readable yet
    static char lastCombo[64] = { 0 };
    if (strcmp(combo, lastCombo) == 0) return;                                  // Name/Main/Sub unchanged -> nothing to do
    strncpy(lastCombo, combo, sizeof(lastCombo) - 1); lastCombo[sizeof(lastCombo) - 1] = 0;
    const char* target = 0; char fb[48] = { 0 };
    if (profile_exists(combo)) target = combo;                                  // (1) exact Name/Main/Sub profile
    else {                                                                      // (2) this character's last manual profile
        PlayerInfo me; if (read_player(me) && me.name[0]) { const char* lp = last_char_profile(me.name); if (lp && lp[0] && profile_exists(lp)) { strncpy(fb, lp, 47); fb[47] = 0; target = fb; } }
    }
    if (target && strcmp(g_active, target) != 0) profile_load(target);          // don't reload the already-active one (would fight a manual load)
}

// ---- "unsaved changes" tracking : snapshot the persisted fields, compare to live ----
static UiConfig g_snap; static bool g_snapValid = false;
static bool box_eq(const BoxStyle& a, const BoxStyle& b) {
    return a.on == b.on && a.border == b.border && a.alpha == b.alpha && a.themeCopy == b.themeCopy && a.theme == b.theme && a.lum == b.lum && a.hue == b.hue;
}
static bool persist_eq(const UiConfig& a, const UiConfig& b) {
    if (a.partyShow != b.partyShow || a.allyShow != b.allyShow || a.tgtShow != b.tgtShow || a.plrShow != b.plrShow) return false;
    if (a.skinTheme != b.skinTheme || a.fontFace != b.fontFace || a.skinLum != b.skinLum || a.skinHue != b.skinHue || a.skinBoxAlpha != b.skinBoxAlpha) return false;
    if (a.allyThemeCopy != b.allyThemeCopy || a.allyTheme != b.allyTheme || a.allyLum != b.allyLum || a.allyHue != b.allyHue || a.allyBoxAlpha != b.allyBoxAlpha) return false;
    if (a.buffScale != b.buffScale) return false;
    if (a.buffMax != b.buffMax) return false;
    if (a.buffRows != b.buffRows) return false;
    if (a.uiStyle != b.uiStyle || a.uiColor != b.uiColor || a.uiAccent != b.uiAccent || a.uiCursor != b.uiCursor || a.hidePeekMode != b.hidePeekMode) return false;
    if (a.cursorScale != b.cursorScale) return false;
    for (int i = 0; i < 6; ++i) if (a.partyRef[i] != b.partyRef[i]) return false;
    if (a.partyBottomY != b.partyBottomY) return false;
    if (a.partyRefX[0] != b.partyRefX[0] || a.partyRefX[1] != b.partyRefX[1]) return false;
    if (a.zonePanelX != b.zonePanelX || a.zonePanelY != b.zonePanelY) return false;   // dragging the Zones panel is a layout edit -> must mark dirty
    for (int i = 0; i < 4; ++i) if (a.allyRefY[i] != b.allyRefY[i]) return false;
    if (a.lang != b.lang) return false;   // language is part of a profile : toggling FR/EN marks it modified so Save keeps it
    if (a.guideGroupCount != b.guideGroupCount) return false;
    for (int i = 0; i < a.guideGroupCount; ++i) {
        if (a.guideGroup[i].x != b.guideGroup[i].x || a.guideGroup[i].y != b.guideGroup[i].y ||
            a.guideGroup[i].w != b.guideGroup[i].w || a.guideGroup[i].h != b.guideGroup[i].h ||
            a.guideGroup[i].role != b.guideGroup[i].role ||
            strcmp(a.guideGroup[i].name, b.guideGroup[i].name) != 0) return false;
        for (int k = 0; k < ZPERM_COUNT; ++k) if (a.guideGroup[i].allow[k] != b.guideGroup[i].allow[k]) return false;
    }
    for (int k = 0; k < 3; ++k) {
        if (a.barHeight[k] != b.barHeight[k] || a.barWidth[k] != b.barWidth[k] || a.badgeScale[k] != b.badgeScale[k]) return false;
        if (a.gaugeStyle[k] != b.gaugeStyle[k] || a.jobBadge[k] != b.jobBadge[k] || a.cast[k] != b.cast[k]) return false;
    }
    if (a.dist[0] != b.dist[0] || a.dist[1] != b.dist[1] || a.dist[2] != b.dist[2]) return false;
    if (a.distColClose != b.distColClose || a.distColNormal != b.distColNormal || a.distColFar != b.distColFar) return false;
    if (a.borderCost != b.borderCost || a.animHP != b.animHP || a.animTP != b.animTP) return false;
    for (int g = 0; g < 2; ++g) for (int k = 0; k < TE_COUNT; ++k) {
        const TextStyle& x = a.text[g][k], & y = b.text[g][k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.color != y.color) return false;
        if (x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn) return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (a.border[i] != b.border[i]) return false;
        if (a.box[i].posSet != b.box[i].posSet || a.box[i].x != b.box[i].x ||
            a.box[i].y != b.box[i].y || a.box[i].scale != b.box[i].scale) return false;
    }
    // Target module : theme / sizes / typography / placement all belong to the profile.
    if (a.tgtBox != b.tgtBox || a.tgtBoxAlpha != b.tgtBoxAlpha || a.tgtThemeCopy != b.tgtThemeCopy || a.tgtTheme != b.tgtTheme || a.tgtLum != b.tgtLum || a.tgtHue != b.tgtHue) return false;
    if (a.tgtScale != b.tgtScale || a.tgtNameHostile != b.tgtNameHostile || a.tgtDebuffs != b.tgtDebuffs || a.tgtTimers != b.tgtTimers) return false;
    if (a.tgtBuffPos != b.tgtBuffPos) return false;
    if (a.tgtSpeed != b.tgtSpeed || a.tgtTH != b.tgtTH) return false;
    if (a.tgtSpeedIcon != b.tgtSpeedIcon || a.tgtThIcon != b.tgtThIcon) return false;
    if (a.tgtRange != b.tgtRange || a.tgtCast != b.tgtCast || a.tgtCastDemo != b.tgtCastDemo || a.tgtSub != b.tgtSub || a.tgtSubPos != b.tgtSubPos) return false;
    if (a.tgtBuffMax != b.tgtBuffMax) return false;
    if (a.tgtBarH != b.tgtBarH || a.tgtBarW != b.tgtBarW || a.tgtIconSz != b.tgtIconSz) return false;
    if (a.tgtDetailIconSz != b.tgtDetailIconSz) return false;
    if (a.tgtRangeH != b.tgtRangeH || a.tgtRangeMin != b.tgtRangeMin) return false;
    if (a.tgtPosSet != b.tgtPosSet || a.tgtX != b.tgtX || a.tgtY != b.tgtY) return false;
    if (a.tgtCenterH != b.tgtCenterH || a.tgtCenterV != b.tgtCenterV) return false;
    // Player Hub module : toggles / sizes all belong to the profile.
    if (a.plrBox != b.plrBox || a.plrBoxAlpha != b.plrBoxAlpha || a.plrScale != b.plrScale) return false;
    if (a.plrThemeCopy != b.plrThemeCopy || a.plrTheme != b.plrTheme || a.plrLum != b.plrLum || a.plrHue != b.plrHue) return false;
    if (a.plrPosSet != b.plrPosSet || a.plrX != b.plrX || a.plrY != b.plrY) return false;
    if (a.plrCenterH != b.plrCenterH || a.plrCenterV != b.plrCenterV) return false;
    if (a.plrEmblem != b.plrEmblem || a.plrName != b.plrName || a.plrLvl != b.plrLvl) return false;
    if (a.plrHp != b.plrHp || a.plrMp != b.plrMp || a.plrTp != b.plrTp) return false;
    if (a.plrBuffs != b.plrBuffs || a.plrBuffMax != b.plrBuffMax || a.plrSpeed != b.plrSpeed || a.plrGil != b.plrGil || a.plrEquip != b.plrEquip || a.plrCast != b.plrCast || a.plrCastDemo != b.plrCastDemo) return false;
    if (a.plrEqCell != b.plrEqCell || a.plrEqThemeBorder != b.plrEqThemeBorder || a.plrEqColor != b.plrEqColor || a.plrEqPlace != b.plrEqPlace) return false;
    if (a.plrEqCellBgCustom != b.plrEqCellBgCustom || a.plrEqCellBg != b.plrEqCellBg) return false;
    if (a.plrEquipDetach != b.plrEquipDetach || a.plrEquipPosSet != b.plrEquipPosSet || a.plrEquipX != b.plrEquipX || a.plrEquipY != b.plrEquipY || a.plrEquipScale != b.plrEquipScale || a.plrEqGilPlace != b.plrEqGilPlace) return false;
    if (!box_eq(a.plrEqBox, b.plrEqBox)) return false;   // DETACHED equipment box chrome
    if (a.mmShow != b.mmShow || a.mmPosSet != b.mmPosSet || a.mmX != b.mmX || a.mmY != b.mmY || a.mmScale != b.mmScale || a.mmZoom != b.mmZoom) return false;
    if (a.mmShape != b.mmShape || a.mmFrame != b.mmFrame || a.mmFrameColor != b.mmFrameColor || a.mmBgAlpha != b.mmBgAlpha || a.mmMarkerScale != b.mmMarkerScale || a.mmPC != b.mmPC || a.mmNPC != b.mmNPC || a.mmMob != b.mmMob) return false;
    if (a.mmTgtLine != b.mmTgtLine || a.mmTgtLineCol != b.mmTgtLineCol || a.mmRing != b.mmRing || a.mmRingR != b.mmRingR || a.mmRingCol != b.mmRingCol) return false;
    if (a.mmClock != b.mmClock || a.mmClkTime != b.mmClkTime || a.mmClkDay != b.mmClkDay || a.mmClkMoon != b.mmClkMoon || a.mmClkReal != b.mmClkReal || a.mmMapSize != b.mmMapSize || a.mmClockPos != b.mmClockPos) return false;
    if (a.mmBezelW != b.mmBezelW || a.mmCardSz != b.mmCardSz || a.mmBezel != b.mmBezel || a.mmSqBorder != b.mmSqBorder) return false;
    if (a.wsShow != b.wsShow || a.wsScale != b.wsScale || a.wsX != b.wsX || a.wsY != b.wsY || a.wsFont != b.wsFont || a.wsFx != b.wsFx || a.wsNameCol != b.wsNameCol || a.wsDmgCol1 != b.wsDmgCol1 || a.wsDmgCol2 != b.wsDmgCol2) return false;
    if (a.scShow != b.scShow || a.scScale != b.scScale || a.scX != b.scX || a.scY != b.scY || a.scNearby != b.scNearby) return false;
    if (a.tpShow != b.tpShow || a.tpScale != b.tpScale || a.tpX != b.tpX || a.tpY != b.tpY || a.tpCount != b.tpCount || a.tpIcon != b.tpIcon) return false;
    for (int k = 0; k < TP_TE_COUNT; ++k) {
        const TextStyle& x = a.tpText[k], & y = b.tpText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.hlShow != b.hlShow || a.hlScale != b.hlScale || a.hlX != b.hlX || a.hlY != b.hlY || a.hlCount != b.hlCount || a.hlDist != b.hlDist || a.hlTgt != b.hlTgt) return false;
    for (int k = 0; k < HL_TE_COUNT; ++k) {
        const TextStyle& x = a.hlText[k], & y = b.hlText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.pwShow != b.pwShow || a.pwScale != b.pwScale || a.pwX != b.pwX || a.pwY != b.pwY || a.pwMode != b.pwMode || a.pwRate != b.pwRate || a.pwLayout != b.pwLayout || a.pwDisplay != b.pwDisplay) return false;
    for (int k = 0; k < PW_TE_COUNT; ++k) {
        const TextStyle& x = a.pwText[k], & y = b.pwText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.grimShow != b.grimShow || a.grimScale != b.grimScale || a.grimX != b.grimX || a.grimY != b.grimY || a.grimArt != b.grimArt) return false;
    for (int k = 0; k < GRIM_TE_COUNT; ++k) {
        const TextStyle& x = a.grimText[k], & y = b.grimText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.ztShow != b.ztShow || a.ztScale != b.ztScale || a.ztX != b.ztX || a.ztY != b.ztY || a.ztVariant != b.ztVariant || a.ztHeader != b.ztHeader) return false;
    if (a.ztSheolSeg != b.ztSheolSeg || a.ztSheolRes != b.ztSheolRes || a.ztSheolJoke != b.ztSheolJoke) return false;
    if (a.ztLbFloor != b.ztLbFloor || a.ztLbCur != b.ztLbCur || a.ztLbRun != b.ztLbRun || a.ztLbChips != b.ztLbChips) return false;
    if (a.ztLbName != b.ztLbName || a.ztLbBarW != b.ztLbBarW || a.ztLbBarH != b.ztLbBarH) return false;
    if (a.ztDyTimer != b.ztDyTimer || a.ztDyKi != b.ztDyKi || a.ztDyBarW != b.ztDyBarW || a.ztDyBarH != b.ztDyBarH || a.ztDyDot != b.ztDyDot) return false;
    if (a.ztAbTimer != b.ztAbTimer || a.ztAbLights != b.ztAbLights || a.ztAbBarW != b.ztAbBarW || a.ztAbBarH != b.ztAbBarH
        || a.ztAbLightW != b.ztAbLightW || a.ztAbLightH != b.ztAbLightH) return false;
    if (a.ztOmObj != b.ztOmObj || a.ztOmCount != b.ztOmCount || a.ztOmRows != b.ztOmRows) return false;
    if (a.ztNyFloor != b.ztNyFloor || a.ztNyTime != b.ztNyTime || a.ztNyObj != b.ztNyObj || a.ztNyRestr != b.ztNyRestr
        || a.ztNyComp != b.ztNyComp || a.ztNyRate != b.ztNyRate || a.ztNyTok != b.ztNyTok) return false;
    if (a.ztShFam != b.ztShFam || a.ztShIcon != b.ztShIcon || a.ztShDot != b.ztShDot) return false;
    for (int k = 0; k < ZT_TE_COUNT; ++k) {
        const TextStyle& x = a.ztText[k], & y = b.ztText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.epShow != b.epShow || a.epScale != b.epScale || a.epX != b.epX || a.epY != b.epY || a.epColl != b.epColl) return false;
    if (strcmp(a.epTrack, b.epTrack) != 0) return false;   // char[] : compare CONTENT, not the array address
    for (int k = 0; k < EP_TE_COUNT; ++k) {
        const TextStyle& x = a.epText[k], & y = b.epText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.scTitle != b.scTitle || a.scTimer != b.scTimer || a.scStep != b.scStep || a.scProps != b.scProps || a.scList != b.scList || a.scListGap != b.scListGap) return false;
    for (int k = 0; k < SC_TE_COUNT; ++k) {
        const TextStyle& x = a.scText[k], & y = b.scText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    if (a.plrBarH != b.plrBarH || a.plrBarW != b.plrBarW || a.plrIconSz != b.plrIconSz) return false;
    if (a.plrBarGap != b.plrBarGap || a.plrEmblemSz != b.plrEmblemSz) return false;
    for (int k = 0; k < PLR_TE_COUNT; ++k) {
        const TextStyle& x = a.plrText[k], & y = b.plrText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.color != y.color) return false;
        if (x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn) return false;
    }
    for (int k = 0; k < MM_TE_COUNT; ++k) {
        const TextStyle& x = a.mmText[k], & y = b.mmText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.color != y.color) return false;
        if (x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn) return false;
    }
    for (int k = 0; k < TGT_TE_COUNT; ++k) {
        const TextStyle& x = a.tgtText[k], & y = b.tgtText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.color != y.color) return false;
        if (x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn) return false;
    }
    // Timers module : box / layout / display / self-cast filter / typography all belong to the profile.
    if (a.tmShow != b.tmShow || a.tmScale != b.tmScale || a.tmX != b.tmX || a.tmY != b.tmY || a.tmMax != b.tmMax || a.tmTitle != b.tmTitle) return false;
    if (a.tmMerged != b.tmMerged || a.tmRX != b.tmRX || a.tmRY != b.tmRY || a.tmIconScale != b.tmIconScale || a.tmRowGap != b.tmRowGap) return false;
    // per-module box appearance (shared BoxStyle)
    if (!box_eq(a.scBox, b.scBox) || !box_eq(a.tpBox, b.tpBox) || !box_eq(a.hlBox, b.hlBox) || !box_eq(a.pwBox, b.pwBox) || !box_eq(a.ztBox, b.ztBox) || !box_eq(a.tmBox, b.tmBox) || !box_eq(a.mmBox, b.mmBox) || !box_eq(a.epBox, b.epBox)) return false;
    if (a.tmDurMode != b.tmDurMode || a.tmRecMode != b.tmRecMode || a.tmOthers != b.tmOthers || a.tmMine != b.tmMine || a.tmBuffSrc != b.tmBuffSrc || a.tmSpAlert != b.tmSpAlert) return false;
    if (a.tmAllyGroup != b.tmAllyGroup || a.tmPreset != b.tmPreset) return false;
    if (a.tmFocusWarn != b.tmFocusWarn || a.tmFocusHold != b.tmFocusHold) return false;
    for (int j = 1; j <= 23; ++j) {                                          // Timers "track per job" blacklist -> part of the profile (so Save picks it up)
        if (a.tmTrackOffN[j] != b.tmTrackOffN[j]) return false;
        for (int i = 0; i < a.tmTrackOffN[j]; ++i) if (a.tmTrackOff[j][i] != b.tmTrackOff[j][i]) return false;
    }
    for (int k = 0; k < TM_TE_COUNT; ++k) {
        const TextStyle& x = a.tmText[k], & y = b.tmText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    // Debuffs module : detach toggle / box / layout / display / typography.
    if (a.dbShow != b.dbShow || a.dbScale != b.dbScale || a.dbX != b.dbX || a.dbY != b.dbY || a.dbMax != b.dbMax) return false;
    if (a.dbHeader != b.dbHeader || a.dbDisp != b.dbDisp || a.dbIconScale != b.dbIconScale || a.dbRowGap != b.dbRowGap) return false;
    if (!box_eq(a.dbBox, b.dbBox)) return false;
    for (int k = 0; k < DB_TE_COUNT; ++k) {
        const TextStyle& x = a.dbText[k], & y = b.dbText[k];
        if (x.face != y.face || x.size != y.size || x.outline != y.outline || x.bold != y.bold || x.italic != y.italic || x.upper != y.upper || x.colorOn != y.colorOn || x.color != y.color) return false;
    }
    return true;
}
void profile_mark_clean() { g_snap = ui_config(); g_snapValid = true; }
bool profile_dirty()      { return g_snapValid && !persist_eq(g_snap, ui_config()); }
bool profile_delete(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    bool ok = DeleteFileA(p) != 0;
    profile_refresh();
    if (g_active[0] && strcmp(g_active, name) == 0) set_active_profile("");   // deleting the active profile clears it
    return ok;
}

void reset_boxes() {   // edit-mode Default : positions + sizes only
    UiConfig& c = ui_config();
    for (int i = 0; i < 3; ++i) c.box[i] = BoxLayout();   // posSet=false, x=y=0, scale=1
    for (int i = 0; i < 6; ++i) c.partyRef[i] = -1.0f;
    c.partyBottomY = -1.0f;
    c.partyRefX[0] = c.partyRefX[1] = -1.0f;
    for (int i = 0; i < 4; ++i) c.allyRefY[i] = -1.0f;
    save_ui_config();
    request_scale_baseline_reset();   // scales went back to 1.0 -> adopt as baseline (no re-anchor)
}

// top Y (px) of the party-role zone for `count` members (its rect top drives the party box), or -1 if none.
float guide_party_top(int count, float sh) {
    UiConfig& c = ui_config();
    for (int g = 0; g < c.guideGroupCount; ++g) if (c.guideGroup[g].role == count) return c.guideGroup[g].y * sh;
    return -1.0f;
}

// ar[0..3] = {A1 top, A1 bottom, A2 top, A2 bottom} (fractions) from the alliance-role zones (7 = A1, 8 = A2).
bool guide_alliance_refs(float* ar) {
    UiConfig& c = ui_config(); int a1 = -1, a2 = -1;
    for (int g = 0; g < c.guideGroupCount; ++g) { if (c.guideGroup[g].role == 7) a1 = g; else if (c.guideGroup[g].role == 8) a2 = g; }
    if (a1 < 0 || a2 < 0) return false;
    ar[0] = c.guideGroup[a1].y; ar[1] = c.guideGroup[a1].y + c.guideGroup[a1].h;
    ar[2] = c.guideGroup[a2].y; ar[3] = c.guideGroup[a2].y + c.guideGroup[a2].h;
    return true;
}

// each user-drawn ZONE rectangle in pixels (from its stored fractions). Returns the number written.
int guide_zones(float sw, float sh, float* ox, float* oy, float* ow, float* oh, int* ogrp, int cap) {
    UiConfig& c = ui_config(); int n = 0;
    for (int g = 0; g < c.guideGroupCount && n < cap; ++g) {
        ox[n] = c.guideGroup[g].x * sw; oy[n] = c.guideGroup[g].y * sh;
        ow[n] = c.guideGroup[g].w * sw; oh[n] = c.guideGroup[g].h * sh; ogrp[n] = g; ++n;
    }
    return n;
}

// P3 : if the box rect (ex,ey,ew,eh) overlaps a zone that FORBIDS `perm`, push it out by the smallest move.
void guide_push_out(int perm, float sw, float sh, float& ex, float& ey, float ew, float eh) {
    UiConfig& c = ui_config();
    float zx[GUIDE_GROUPS_MAX], zy[GUIDE_GROUPS_MAX], zw[GUIDE_GROUPS_MAX], zh[GUIDE_GROUPS_MAX]; int zg[GUIDE_GROUPS_MAX];
    const int nz = guide_zones(sw, sh, zx, zy, zw, zh, zg, GUIDE_GROUPS_MAX);
    for (int it = 0; it < 4; ++it) {                          // a few passes to settle overlapping zones
        bool moved = false;
        for (int i = 0; i < nz; ++i) {
            if (perm >= 0 && perm < ZPERM_COUNT && c.guideGroup[zg[i]].allow[perm]) continue;   // allowed here
            if (ex < zx[i] + zw[i] && ex + ew > zx[i] && ey < zy[i] + zh[i] && ey + eh > zy[i]) {
                const float pL = zx[i] - (ex + ew), pR = (zx[i] + zw[i]) - ex;   // move left (neg) / right (pos)
                const float pU = zy[i] - (ey + eh), pD = (zy[i] + zh[i]) - ey;   // move up (neg) / down (pos)
                const float ax = ((pL < 0 ? -pL : pL) < (pR < 0 ? -pR : pR)) ? pL : pR;
                const float ay = ((pU < 0 ? -pU : pU) < (pD < 0 ? -pD : pD)) ? pU : pD;
                if ((ax < 0 ? -ax : ax) <= (ay < 0 ? -ay : ay)) ex += ax; else ey += ay;
                moved = true;
            }
        }
        if (!moved) break;
    }
}

void reset_ui_config() {   // general Default : everything
    UiConfig& c = ui_config();
    c.partyShow = 1; c.allyShow = 1; c.tgtShow = 1; c.plrShow = 1;
    c.skinTheme = 0; c.skinLum = 0.0f; c.skinHue = 0; c.skinBoxAlpha = 1.0f; c.fontFace = 0; c.buffScale = 0.92f; c.buffMax = 20; c.buffRows = 2; c.uiStyle = 0; c.uiColor = 0; c.uiAccent = 0; c.uiCursor = 0; c.hidePeekMode = 0; c.cursorScale = 1.0f;
    c.allyThemeCopy = 1; c.allyTheme = 0; c.allyLum = 0.0f; c.allyHue = 0; c.allyBoxAlpha = 1.0f;
    for (int k = 0; k < 3; ++k) { c.barHeight[k] = 1.0f; c.barWidth[k] = 1.0f; c.badgeScale[k] = 1.0f; c.gaugeStyle[k] = 0; c.jobBadge[k] = 2; c.cast[k] = true; }
    c.dist[0] = c.dist[1] = c.dist[2] = true;
    c.distColClose = 0xFF8FC6FF; c.distColNormal = 0xFFE7C95A; c.distColFar = 0xFFE76C6C;   // distance-zone colours back to defaults
    c.border[0] = c.border[1] = c.border[2] = c.borderCost = true;   // all borders back on
    c.animHP = c.animTP = true;
    for (int g = 0; g < 2; ++g) for (int k = 0; k < TE_COUNT; ++k) c.text[g][k] = TextStyle();   // typography back to defaults
    // Target module back to defaults (theme / sizes / typography / placement)
    c.tgtBox = 1; c.tgtBoxAlpha = 1.0f; c.tgtThemeCopy = 0; c.tgtTheme = 0; c.tgtLum = 0.0f; c.tgtHue = 0; c.tgtScale = 1.0f; c.tgtNameHostile = 1; c.tgtSpeed = 1; c.tgtSpeedIcon = 0; c.tgtTH = 1; c.tgtThIcon = 0; c.tgtRange = 1; c.tgtCast = 1; c.tgtCastDemo = 0; c.tgtSub = 1; c.tgtDebuffs = 1; c.tgtBuffMax = 20; c.tgtBuffPos = 0; c.tgtTimers = 1;
    c.tgtBarH = 1.0f; c.tgtBarW = 1.0f; c.tgtIconSz = 1.0f; c.tgtDetailIconSz = 1.6f; c.tgtRangeH = 1.0f; c.tgtRangeMin = 0;
    c.tgtPosSet = false; c.tgtX = 0.0f; c.tgtY = 0.0f; c.tgtCenterH = 0; c.tgtCenterV = 0;
    for (int k = 0; k < TGT_TE_COUNT; ++k) c.tgtText[k] = TextStyle();
    // Player Hub module back to defaults
    c.plrBox = 1; c.plrBoxAlpha = 1.0f; c.plrThemeCopy = 1; c.plrTheme = 0; c.plrLum = 0.0f; c.plrHue = 0; c.plrScale = 1.0f; c.plrEmblem = 1; c.plrName = 1; c.plrLvl = 1; c.plrHp = 1; c.plrMp = 1; c.plrTp = 1; c.plrGil = 1; c.plrSpeed = 1; c.plrCast = 1; c.plrCastDemo = 0; c.plrEquip = 1; c.plrEqCell = 1.0f; c.plrEqThemeBorder = 1; c.plrEqColor = 0xFF6699BBu; c.plrEqPlace = 0; c.plrEqCellBgCustom = 0; c.plrEqCellBg = 0xE0121620u; c.plrEquipDetach = 0; c.plrEquipPosSet = false; c.plrEquipX = 0.0f; c.plrEquipY = 0.0f; c.plrEquipScale = 1.0f; c.plrEqGilPlace = 0; c.mmShow = 1; c.mmPosSet = false; c.mmX = 0.0f; c.mmY = 0.0f; c.mmScale = 1.0f; c.mmZoom = 2.0f; c.mmShape = 0; c.mmFrame = 1; c.mmFrameColor = 0xFF6699BBu; c.mmBgAlpha = 0.0f; c.mmMarkerScale = 1.0f; c.mmPC = 1; c.mmNPC = 1; c.mmMob = 1; c.mmTgtLine = 1; c.mmTgtLineCol = 0xFFFF6A6Au; c.mmRing = 0; c.mmRingR = 20.0f; c.mmRingCol = 0xFF66E0FFu; c.mmClock = 1; c.mmClkTime = 1; c.mmClkDay = 1; c.mmClkMoon = 1; c.mmClkReal = 1; c.mmMapSize = 1.0f; c.mmBezelW = 1.0f; c.mmCardSz = 1.0f; c.mmBezel = 1; c.mmSqBorder = 1.0f; c.wsShow = 1; c.wsScale = 1.0f; c.wsX = 0.5f; c.wsY = 0.36f; c.wsFont = 0; c.wsFx = 1; c.wsNameCol = 0xFFFFA518u; c.wsDmgCol1 = 0xFFFFF024u; c.wsDmgCol2 = 0xFFFF5A0Au; c.scShow = 1; c.scScale = 1.0f; c.scX = 0.78f; c.scY = 0.06f; c.scTitle = 1; c.scTimer = 1; c.scStep = 1; c.scProps = 1; c.scList = 1; c.scListGap = 1.0f; for (int k = 0; k < SC_TE_COUNT; ++k) c.scText[k] = TextStyle(); c.tpShow = 1; c.tpScale = 1.0f; c.tpX = 0.72f; c.tpY = 0.30f; c.tpCount = 10; c.tpIcon = 1; for (int k = 0; k < TP_TE_COUNT; ++k) c.tpText[k] = TextStyle(); c.plrBuffs = 1; c.plrBuffMax = 24; c.plrBarH = 1.0f; c.plrBarW = 1.0f; c.plrIconSz = 1.0f;
    c.plrBarGap = 1.0f; c.plrEmblemSz = 1.0f; c.plrPosSet = false; c.plrX = 0.0f; c.plrY = 0.0f; c.plrCenterH = 0; c.plrCenterV = 0;
    for (int k = 0; k < PLR_TE_COUNT; ++k) c.plrText[k] = TextStyle();
    for (int k = 0; k < MM_TE_COUNT; ++k) c.mmText[k] = TextStyle();
    // Modules that were MISSING from the reset above -> restore from fresh defaults so "Reset all" truly resets every
    // profile field : Hate List / PointWatch / Grimoire / Zone Tracker, the per-module box appearances, and stragglers.
    static const UiConfig d{};   // default-constructed once ; source of every default value below ({} : newer MSVC needs a const object value-initialised)
    c.hlShow = d.hlShow; c.hlScale = d.hlScale; c.hlX = d.hlX; c.hlY = d.hlY; c.hlCount = d.hlCount; c.hlDist = d.hlDist; c.hlTgt = d.hlTgt;
    c.pwShow = d.pwShow; c.pwScale = d.pwScale; c.pwX = d.pwX; c.pwY = d.pwY; c.pwMode = d.pwMode; c.pwLayout = d.pwLayout; c.pwDisplay = d.pwDisplay; c.pwRate = d.pwRate;
    c.grimShow = d.grimShow; c.grimScale = d.grimScale; c.grimX = d.grimX; c.grimY = d.grimY; c.grimArt = d.grimArt;
    c.ztShow = d.ztShow; c.ztScale = d.ztScale; c.ztX = d.ztX; c.ztY = d.ztY; c.ztVariant = d.ztVariant; c.ztHeader = d.ztHeader; c.ztSheolSeg = d.ztSheolSeg; c.ztSheolRes = d.ztSheolRes; c.ztSheolJoke = d.ztSheolJoke;
    c.ztLbFloor = d.ztLbFloor; c.ztLbCur = d.ztLbCur; c.ztLbRun = d.ztLbRun; c.ztLbChips = d.ztLbChips;
    c.ztLbName = d.ztLbName; c.ztLbBarW = d.ztLbBarW; c.ztLbBarH = d.ztLbBarH;
    c.ztDyTimer = d.ztDyTimer; c.ztDyKi = d.ztDyKi; c.ztDyBarW = d.ztDyBarW; c.ztDyBarH = d.ztDyBarH; c.ztDyDot = d.ztDyDot;
    c.ztAbTimer = d.ztAbTimer; c.ztAbLights = d.ztAbLights; c.ztAbBarW = d.ztAbBarW; c.ztAbBarH = d.ztAbBarH; c.ztAbLightW = d.ztAbLightW; c.ztAbLightH = d.ztAbLightH;
    c.ztOmObj = d.ztOmObj; c.ztOmCount = d.ztOmCount; c.ztOmRows = d.ztOmRows;
    c.ztNyFloor = d.ztNyFloor; c.ztNyTime = d.ztNyTime; c.ztNyObj = d.ztNyObj; c.ztNyRestr = d.ztNyRestr; c.ztNyComp = d.ztNyComp; c.ztNyRate = d.ztNyRate; c.ztNyTok = d.ztNyTok;
    c.ztShFam = d.ztShFam; c.ztShIcon = d.ztShIcon; c.ztShDot = d.ztShDot;
    c.epShow = d.epShow; c.epScale = d.epScale; c.epX = d.epX; c.epY = d.epY; c.epColl = d.epColl;
    lstrcpynA(c.epTrack, d.epTrack, sizeof(c.epTrack));   // char[] : copy the CONTENT (plain '=' won't compile)
    c.scBox = d.scBox; c.tpBox = d.tpBox; c.hlBox = d.hlBox; c.pwBox = d.pwBox; c.ztBox = d.ztBox; c.mmBox = d.mmBox; c.epBox = d.epBox; c.dbBox = d.dbBox; c.plrEqBox = d.plrEqBox;
    c.dbShow = d.dbShow; c.dbScale = d.dbScale; c.dbX = d.dbX; c.dbY = d.dbY; c.dbMax = d.dbMax; c.dbHeader = d.dbHeader; c.dbDisp = d.dbDisp; c.dbIconScale = d.dbIconScale; c.dbRowGap = d.dbRowGap;
    c.tgtSubPos = d.tgtSubPos; c.mmClockPos = d.mmClockPos; c.scNearby = d.scNearby;
    for (int k = 0; k < HL_TE_COUNT; ++k)   c.hlText[k]   = TextStyle();
    for (int k = 0; k < PW_TE_COUNT; ++k)   c.pwText[k]   = TextStyle();
    for (int k = 0; k < GRIM_TE_COUNT; ++k) c.grimText[k] = TextStyle();
    for (int k = 0; k < ZT_TE_COUNT; ++k)   c.ztText[k]   = TextStyle();
    for (int k = 0; k < EP_TE_COUNT; ++k)   c.epText[k]   = TextStyle();
    for (int k = 0; k < DB_TE_COUNT; ++k)   c.dbText[k]   = TextStyle();
    reset_boxes();   // (also saves)
}

// index 0 = Default (keep the layout face). The rest are common, readable Windows GDI faces (a face that
// isn't installed just falls back to a system default when GDI creates it -> always safe to list).
static const char* FACE_LABEL[] = {
    "Default", "Segoe UI", "Segoe UI Semibold", "Roboto", "Open Sans", "Arial", "Tahoma", "Verdana",
    "Calibri", "Candara", "Corbel", "Trebuchet MS", "Franklin Gothic Medium", "Century Gothic",
    "Bahnschrift", "Lucida Sans Unicode", "Microsoft Sans Serif", "Consolas", "Courier New", "Georgia",
};
static const char* FACE_NAME[] = {
    "", "Segoe UI", "Segoe UI Semibold", "Roboto", "Open Sans", "Arial", "Tahoma", "Verdana",
    "Calibri", "Candara", "Corbel", "Trebuchet MS", "Franklin Gothic Medium", "Century Gothic",
    "Bahnschrift", "Lucida Sans Unicode", "Microsoft Sans Serif", "Consolas", "Courier New", "Georgia",
};
static const int NFACE = (int)(sizeof(FACE_LABEL) / sizeof(FACE_LABEL[0]));

const char* ui_text_elem_label(int e) {
    static const char* L[TE_COUNT] = { "Name", "HP", "MP", "TP", "Cast", "Job Badge", "Distance", "Interface", "Cost box" };
    return (e >= 0 && e < TE_COUNT) ? L[e] : "";
}
int         ui_font_count()        { return NFACE; }
const char* ui_font_label(int i)   { return (i >= 0 && i < NFACE) ? FACE_LABEL[i] : ""; }
const char* ui_font_face(int i)    { return (i >= 0 && i < NFACE) ? FACE_NAME[i]  : ""; }

} // namespace aio
