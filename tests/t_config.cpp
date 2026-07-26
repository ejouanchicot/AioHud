// t_config.cpp -- the config round-trip : everything save_config_to writes, load_config_from must read back.
//
// WHY THIS EXISTS. ui_config.cpp is ~1250 lines of hand-written serialisation over ~110 keys, and the reader is
// one `else if` chain long enough that it had to be split into five out-of-line helpers just to stay under
// MSVC's nesting limit. Nothing checked that the two halves agreed. They have already disagreed in production:
// ui_config.cpp:606 carries the scar -- "renamed from mm3= : it collided with the clock's mm3= and never
// loaded", i.e. a block of minimap settings that the user could change, that was written to disk, and that came
// back as the default at every load. Silent, and invisible to every other test.
//
// THE ORACLE is the project's own comparator, persist_eq (exposed as ui_config_persist_eq). That matters: it is
// the single definition of "which fields belong to a profile", it is hand-maintained across ~290 fields, and it
// is the other half of the same rot. A field added to the writer and the reader but forgotten in persist_eq
// makes the "modified" dot lie; a field added to persist_eq but forgotten in the writer fails HERE.
//
// NOT tested here: that the values are semantically right, or that the file format is stable across versions.
// This asks one question only -- does a config survive a trip to disk and back, field for field.
#include "check.h"
#include "model/ui_config.h"
#include "model/paths.h"
#include "model/game_mem.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

using namespace aio;

// ---- stubs : ui_config.cpp reaches for the plugin folder and the logged-in character. Neither exists offline.
// Redirect every path into a scratch dir under %TEMP% and report "no character". These are the ONLY two
// dependencies the config layer has on the rest of the program -- which is itself worth knowing.
namespace aio {

static char g_testDir[MAX_PATH];
const char* plugin_dir() {
    if (!g_testDir[0]) {
        char t[MAX_PATH]; GetTempPathA(MAX_PATH, t);
        _snprintf(g_testDir, sizeof(g_testDir), "%saiohud_t_%lu", t, (unsigned long)GetCurrentProcessId());
        g_testDir[sizeof(g_testDir) - 1] = 0;
        CreateDirectoryA(g_testDir, NULL);
    }
    return g_testDir;
}
void plugin_path(char* out, int cap, const char* rel) { _snprintf(out, cap, "%s\\%s", plugin_dir(), rel); out[cap - 1] = 0; }
const char* plugin_path_r(const char* rel) { static char b[8][MAX_PATH]; static int k = 0; k = (k + 1) & 7; plugin_path(b[k], MAX_PATH, rel); return b[k]; }
void plugin_path_w(wchar_t* out, int cap, const wchar_t* rel) { (void)rel; if (cap > 0) out[0] = 0; }

bool read_player(PlayerInfo& o) { o = PlayerInfo{}; return false; }   // offline : no character logged in

} // namespace aio

// ---- a deterministic, NON-DEFAULT value for every persisted field we can reach through the public struct.
// The point is that no field keeps its default : a key that is written but never read comes back as the
// default, and a comparison against defaults would not notice.
static void scribble(UiConfig& c, int seed) {
    const int  i1 = seed;                      // ints stay inside the ranges load_config_from clamps to,
    const float f1 = 0.25f + 0.10f * seed;     // otherwise the sanitiser would legitimately change them and
    const unsigned x1 = 0xFF102030u + seed;    // the test would fail on its own bad inputs, not on a real bug.

    c.partyShow = i1 & 1; c.allyShow = !(i1 & 1); c.tgtShow = i1 & 1; c.plrShow = !(i1 & 1);
    c.skinTheme = i1 % 4; c.skinLum = f1 * 0.5f; c.skinHue = x1; c.skinBoxAlpha = f1;
    c.allyThemeCopy = i1 & 1; c.allyTheme = i1 % 3; c.allyLum = f1 * 0.4f; c.allyHue = x1 + 7; c.allyBoxAlpha = f1 * 0.9f;
    c.fontFace = i1 % 3; c.buffScale = 0.5f + 0.25f * seed; c.buffMax = 4 + seed; c.buffRows = 1 + (seed & 1);
    c.uiStyle = i1 % 8; c.uiColor = i1 % 12; c.uiAccent = x1 + 11; c.hidePeekMode = i1 & 1;
    c.cursorScale = 0.6f + 0.2f * seed; c.lang = i1 & 1;

    c.tgtBox = i1 & 1; c.tgtBoxAlpha = f1; c.tgtScale = 0.8f + 0.1f * seed; c.tgtTheme = i1 % 3;
    c.tgtHue = x1 + 3; c.tgtLum = f1 * 0.3f; c.tgtThemeCopy = i1 & 1; c.tgtBarH = 0.7f + 0.1f * seed;
    c.tgtBarW = 0.9f + 0.1f * seed; c.tgtIconSz = 0.8f + 0.1f * seed;
    c.tgtPosSet = i1 & 1; c.tgtX = 0.11f * seed; c.tgtY = 0.13f * seed;

    c.plrPosSet = i1 & 1; c.plrX = 0.17f * seed; c.plrY = 0.19f * seed;
    c.plrScale = 0.9f + 0.1f * seed; c.plrBoxAlpha = f1; c.plrTheme = i1 % 3; c.plrLum = f1 * 0.2f; c.plrHue = x1 + 5;

    c.mmShow = i1 & 1; c.mmPosSet = i1 & 1; c.mmX = 0.21f * seed; c.mmY = 0.23f * seed;
    c.mmScale = 0.8f + 0.1f * seed; c.mmZoom = 2.0f + 1.0f * seed;   // the field whose unclamped load was the 2026-07-26 S0
    c.mmShape = i1 & 1; c.mmFrameColor = x1 + 13; c.mmBgAlpha = f1 * 0.5f;

    c.scShow = i1 & 1; c.scScale = 0.9f + 0.1f * seed; c.scX = 0.29f * seed; c.scY = 0.31f * seed;
    c.tpShow = i1 & 1; c.tpScale = 0.9f + 0.1f * seed; c.tpCount = 3 + seed;
    c.hlShow = i1 & 1; c.hlScale = 0.9f + 0.1f * seed; c.hlCount = 4 + seed;
    c.pwShow = i1 & 1; c.pwScale = 0.9f + 0.1f * seed; c.pwMode = i1 % 3;
    c.grimShow = i1 & 1; c.grimScale = 0.9f + 0.1f * seed;
    c.ztShow = i1 & 1; c.ztScale = 0.9f + 0.1f * seed;
    c.tmShow = i1 & 1; c.tmScale = 0.9f + 0.1f * seed; c.tmMax = 8 + seed; c.tmMerged = i1 & 1;
    c.dbShow = i1 & 1; c.dbScale = 0.9f + 0.1f * seed; c.dbMax = 6 + seed;
    c.epShow = i1 & 1; c.epScale = 0.9f + 0.1f * seed;
    c.wsShow = i1 & 1; c.wsScale = 0.9f + 0.1f * seed; c.wsNameCol = x1 + 17;

    for (int k = 0; k < 3; ++k) {
        c.barHeight[k] = 0.7f + 0.1f * (k + seed); c.barWidth[k] = 0.8f + 0.1f * (k + seed);
        c.badgeScale[k] = 0.9f + 0.1f * (k + seed);
        c.gaugeStyle[k] = (k + seed) % 8; c.jobBadge[k] = (k + seed) % 4;
        c.cast[k] = ((k + seed) & 1) != 0; c.dist[k] = ((k + seed + 1) & 1) != 0; c.border[k] = ((k + seed) & 1) != 0;
        c.box[k].posSet = true; c.box[k].x = 0.05f * (k + seed); c.box[k].y = 0.07f * (k + seed);
        c.box[k].scale = 1.0f + 0.1f * (k + seed);
    }
    c.borderCost = (i1 & 1) != 0; c.animHP = !(i1 & 1); c.animTP = (i1 & 1) != 0;
    c.distColClose = x1 + 21; c.distColNormal = x1 + 23; c.distColFar = x1 + 29;

    // per-group typography : the block that was 40 hand-copies until the 1cf54bb de-duplication.
    for (int g = 0; g < 2; ++g) for (int k = 0; k < TE_COUNT; ++k) {
        TextStyle& t = c.text[g][k];
        t.face = (g + k + seed) % 3; t.size = 0.8f + 0.05f * ((k + seed) % 5);
        t.outline = 0.1f * ((k + seed) % 4); t.color = x1 + (unsigned)(g * 31 + k);
        t.bold = ((k + seed) & 1) != 0; t.italic = ((k + seed) & 2) != 0;
        t.upper = ((k + seed) & 4) != 0; t.colorOn = ((k + seed) & 8) != 0;
    }
}

void test_config() {
    SECTION("config : every persisted field survives a save/load round-trip");

    // Same first call the plugin makes at init : it is what builds data\ + data\profiles\ (profile_save only
    // creates the LEAF directory -- CreateDirectory does not make intermediates). Starting without it is how
    // this test first failed, which is a fair reminder that the config layer has a setup order.
    load_ui_config();

    // A FIXED-POINT test, deliberately -- not "scribble, save, load, compare to what I scribbled". That naive
    // form fails on a healthy config, and finding out why is worth recording : floats are written at 3-5 decimal
    // places (`skinLum=%.3f`, `buffScale=%.4f`, ...) while persist_eq compares them with exact `!=`. A value like
    // 0.175f does not survive "0.175" bit-for-bit, so the naive test compares ACROSS the rounding boundary and
    // reports a defect that is not there. Production never crosses it: profile_mark_clean() always snapshots on
    // the same side of the rounding as the value it will be compared against.
    // So: normalise ONCE through the file, then require that a second trip changes nothing. That still catches
    // the bug this exists for -- a key the writer emits and the reader ignores keeps the SCRIBBLED value at
    // step 3 instead of the stored one -- and it additionally catches an ASYMMETRIC key, one the reader accepts
    // only partially and that would drift a little further on every save/load cycle.
    UiConfig& live = ui_config();

    // 1) a config that shares NO field with the defaults, pushed through the file once to adopt its precision.
    scribble(live, 1);
    CHECK(profile_save("t_roundtrip"));
    CHECK(profile_load("t_roundtrip"));
    const UiConfig stored = ui_config();                // the reference : what the file actually represents

    // 2) overwrite every one of those fields with a DIFFERENT non-default value.
    scribble(live, 2);
    CHECK(!ui_config_persist_eq(stored, live));         // sanity : the scribble really did change something

    // 3) reading the file back must restore every one of them. A key the reader ignores keeps the step-2 value.
    CHECK(profile_load("t_roundtrip"));
    CHECK(ui_config_persist_eq(stored, ui_config()));

    // 4) re-saving what we just loaded and reloading must land on the same config -- no slow drift per cycle.
    CHECK(profile_save("t_roundtrip2"));
    scribble(live, 3);
    CHECK(profile_load("t_roundtrip2"));
    CHECK(ui_config_persist_eq(stored, ui_config()));

    SECTION("config : a corrupt value is clamped, not propagated");
    // The 2026-07-26 S0 in one line : mmZoom read from a hand-edited file used to reach a sprintf unclamped.
    // Write a hostile value directly into the profile file and check the loader refuses it.
    {
        char p[MAX_PATH]; plugin_path(p, sizeof(p), "data\\profiles\\t_evil.txt");
        char dir[MAX_PATH]; plugin_path(dir, sizeof(dir), "data"); CreateDirectoryA(dir, NULL);
        plugin_path(dir, sizeof(dir), "data\\profiles"); CreateDirectoryA(dir, NULL);
        FILE* f = fopen(p, "w");
        if (f) { fputs("mm=1,1,0.5,0.5,1,1e30\nbuffScale=1e30\ncursorScale=-1e30\nbuffMax=99999\n", f); fclose(f); }
        profile_refresh();
        CHECK(profile_load("t_evil"));
        const UiConfig& c = ui_config();
        CHECK(c.mmZoom >= 1.0f && c.mmZoom <= 24.0f);
        CHECK(c.buffScale >= 0.10f && c.buffScale <= 4.0f);
        CHECK(c.cursorScale >= 0.10f && c.cursorScale <= 4.0f);
        CHECK(c.buffMax >= 0 && c.buffMax <= 32);
        profile_delete("t_evil");
    }

    profile_delete("t_roundtrip");
    profile_delete("t_roundtrip2");
}
