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
#include "model/config_rules.h"   // the two pure decisions under test
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
    // buffOrder : a ROTATION, so it stays a valid permutation. Anything else and the loader's repair would
    // legitimately rewrite it, and the round-trip would fail on the test's own bad input, not on a real bug.
    for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) c.buffOrder[i] = (unsigned char)((i + seed) % UiConfig::BUFF_ORDER_N);
    c.buffGroupOff = (unsigned)(0x15u + seed) & ((1u << UiConfig::BUFF_ORDER_N) - 1u);
    for (int i = 0; i < 80; ++i) c.buffStatusOff[i] = (unsigned char)((i * 7 + seed * 13) & 0xFF);
    // arranged in-group prefixes : DISTINCT ids (the loader drops duplicates, so a repeated one would make the
    // round-trip fail on the test's own bad input), and only some groups have one -- absence is a state too.
    for (int g = 0; g < UiConfig::BUFF_ORDER_N; ++g) {
        if ((g + seed) % 3 == 0) { c.buffPinN[g] = 0; continue; }
        const int k = 2 + ((g + seed) % 3);
        for (int q = 0; q < k; ++q) c.buffPin[g][q] = (unsigned short)(40 + g * 20 + q);
        c.buffPinN[g] = (unsigned char)k;
    }

    // per-group typography : the block that was 40 hand-copies until the 1cf54bb de-duplication.
    for (int g = 0; g < 2; ++g) for (int k = 0; k < TE_COUNT; ++k) {
        TextStyle& t = c.text[g][k];
        t.face = (g + k + seed) % 3; t.size = 0.8f + 0.05f * ((k + seed) % 5);
        t.outline = 0.1f * ((k + seed) % 4); t.color = x1 + (unsigned)(g * 31 + k);
        t.bold = ((k + seed) & 1) != 0; t.italic = ((k + seed) & 2) != 0;
        t.upper = ((k + seed) & 4) != 0; t.colorOn = ((k + seed) & 8) != 0;
    }
}

// ---- EVERY persisted byte, not just the ones a test remembered to name.
// `scribble` above is hand-written, so it covers the fields someone thought of -- and the field the reset
// actually forgot in production (scTP) was precisely one nobody had thought of. A reset invariant built on
// scribble is therefore blind in exactly the place the defect lives: proved by mutation on 2026-09-12, a
// reset that deliberately kept one field passed the test.
// So fill the WHOLE struct with a byte pattern and repair only what must stay structurally legal for
// persist_eq to be able to walk it -- the counts that bound its loops, and the two strings it strcmp's.
// Anything the reset then leaves untouched keeps 0x5A garbage and fails, whatever its name.
static void dirty_every_field(UiConfig& c) {
    memset(&c, 0x5A, sizeof(c));
    for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) c.buffOrder[i] = (unsigned char)i;
    for (int g = 0; g < UiConfig::BUFF_ORDER_N; ++g) c.buffPinN[g] = 3;            // <= BUFF_PIN_MAX
    for (int j = 0; j < 24; ++j) c.tmTrackOffN[j] = 4;                             // <= TM_TRACK_MAX
    c.tmBuffOffN = 4;
    c.favColorN = 2;                                                               // (not part of persist_eq)
    c.guideGroupCount = 2;
    for (int i = 0; i < c.guideGroupCount; ++i) c.guideGroup[i].name[19] = 0;
    c.epTrack[sizeof(c.epTrack) - 1] = 0;                                          // strcmp'd by persist_eq
    c.iconPack[sizeof(c.iconPack) - 1] = 0;
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

    SECTION("config : a group split out later lands beside its parent, not at the end");
    // A config written before Enspells/Bar-spells/Spikes/Stat boosts existed lists thirteen groups. The
    // permutation repair alone would append the four missing ones, parking them past Other -- the far end
    // of the strip -- for everyone who had ever opened the editor. They belong beside Enhancing (8), which
    // is what they were split out of.
    {
        char p[MAX_PATH]; plugin_path(p, sizeof(p), "data\\profiles\\t_legacy.txt");
        char dir[MAX_PATH]; plugin_path(dir, sizeof(dir), "data"); CreateDirectoryA(dir, NULL);
        plugin_path(dir, sizeof(dir), "data\\profiles"); CreateDirectoryA(dir, NULL);
        FILE* f = fopen(p, "w");
        if (f) { fputs("buffOrder=0,1,2,3,4,5,6,7,8,9,10,11,12\n", f); fclose(f); }
        profile_refresh();
        CHECK(profile_load("t_legacy"));
        const UiConfig& c = ui_config();
        const unsigned char want[UiConfig::BUFF_ORDER_N] = { 0,1,2,3,4,5,6,7,8, 13,14,15,16, 9,10,11,12 };
        bool same = true;
        for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) if (c.buffOrder[i] != want[i]) same = false;
        CHECK(same);
        if (!same) {
            printf("   got ");
            for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) printf("%u ", (unsigned)c.buffOrder[i]);
            printf("\n");
        }
    }

    SECTION("config : a corrupt value is clamped, not propagated");
    // The 2026-07-26 S0 in one line : mmZoom read from a hand-edited file used to reach a sprintf unclamped.
    // Write a hostile value directly into the profile file and check the loader refuses it.
    {
        char p[MAX_PATH]; plugin_path(p, sizeof(p), "data\\profiles\\t_evil.txt");
        char dir[MAX_PATH]; plugin_path(dir, sizeof(dir), "data"); CreateDirectoryA(dir, NULL);
        plugin_path(dir, sizeof(dir), "data\\profiles"); CreateDirectoryA(dir, NULL);
        FILE* f = fopen(p, "w");
        if (f) { fputs("mm=1,1,0.5,0.5,1,1e30\nbuffScale=1e30\ncursorScale=-1e30\nbuffMax=99999\n"
                       "buffOrder=5,5,5,99\n"
                       "buffPin1=33,43,33,43\n"
                       "buffPin99=1,2,3\n", f); fclose(f); }   // duplicates, an out-of-range group, a line far too short, a repeated status, and a group that does not exist
        profile_refresh();
        CHECK(profile_load("t_evil"));
        const UiConfig& c = ui_config();
        CHECK(c.mmZoom >= 1.0f && c.mmZoom <= 24.0f);
        CHECK(c.buffScale >= 0.10f && c.buffScale <= 4.0f);
        CHECK(c.cursorScale >= 0.10f && c.cursorScale <= 4.0f);
        CHECK(c.buffMax >= 0 && c.buffMax <= 32);
        {   // buffOrder must come back a PERMUTATION whatever the file said : a duplicate or a hole means a
            // whole group of buffs is never drawn, which reads as "my icons vanished", not as a bad file.
            bool seen[UiConfig::BUFF_ORDER_N] = { false }; bool perm = true;
            for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) {
                const unsigned char g = c.buffOrder[i];
                if (g >= UiConfig::BUFF_ORDER_N || seen[g]) { perm = false; break; }
                seen[g] = true;
            }
            CHECK(perm);
            CHECK(c.buffOrder[0] == 5);   // the one valid entry the file gave is honoured, and kept first
        }
        {   // an arranged prefix must come back with no DUPLICATE : one status holding two positions makes the
            // strip order depend on which copy is found first, which is not a thing a user can reason about.
            bool anyDup = false;
            for (int g = 0; g < UiConfig::BUFF_ORDER_N; ++g)
                for (int a = 0; a < c.buffPinN[g]; ++a)
                    for (int b = a + 1; b < c.buffPinN[g]; ++b)
                        if (c.buffPin[g][a] == c.buffPin[g][b]) anyDup = true;
            CHECK(!anyDup);
            CHECK(c.buffPinN[0] <= UiConfig::BUFF_PIN_MAX);
        }
        profile_delete("t_evil");
    }


    // ------------------------------------------------------------------------------------------------------
    // A CONFIG FILE IS A COMPLETE CONFIG. The loader used to OVERLAY : it cleared four dynamic lists by hand
    // and left every other field holding the previous profile's value, so a key the file does not carry was
    // answered by whatever happened to be loaded before -- and the next save wrote that answer into the file.
    // Two profiles quietly mixed, with nothing on screen to explain it.
    //
    // This is not hypothetical and it is not only about old files : MEASURED 2026-09-12, the SHIPPED
    // assets/default_profile.txt carries 106 keys where the writer emits 130 (missing partyShow, allyShow,
    // tgtShow, plrShow, hidePeekMode, iconpack, the whole Debuffs and EmpyPop blocks, mm5, distcol, the five
    // zone-tracker row blocks...). So loading "Default" kept the user's current answer for all of those, and
    // then re-saved it into Default.
    SECTION("config : a profile load REPLACES, it does not overlay the previous one");
    {
        char dir[MAX_PATH];
        plugin_path(dir, sizeof(dir), "data"); CreateDirectoryA(dir, NULL);
        plugin_path(dir, sizeof(dir), "data\\profiles"); CreateDirectoryA(dir, NULL);
        char pe[MAX_PATH], pp[MAX_PATH];
        plugin_path(pe, sizeof(pe), "data\\profiles\\t_empty.txt");
        plugin_path(pp, sizeof(pp), "data\\profiles\\t_partial.txt");
        // The REFERENCE is measured through the production path, not asserted from the struct : loading a file
        // that says nothing is by definition "what a load starts from" (the defaults, plus the once-per-file
        // RDM preset load_config_from seeds -- which is why comparing against a bare UiConfig{} would be wrong
        // here). Two files, identical but for ONE key, so the difference is that key and nothing else.
        FILE* f = fopen(pe, "w"); if (f) { fputs("# nothing at all\n", f); fclose(f); }
        f = fopen(pp, "w");       if (f) { fputs("# nothing but one setting\ntgtScale=1.4500\n", f); fclose(f); }
        profile_refresh();

        scribble(ui_config(), 4);                          // a config sharing no field with the defaults
        CHECK(profile_load("t_empty"));
        const UiConfig base = ui_config();                 // what a load starts from

        scribble(ui_config(), 5);                          // dirty EVERY persisted field again, differently
        CHECK(!ui_config_persist_eq(base, ui_config()));   // sanity : the scribble really did change something
        CHECK(profile_load("t_partial"));

        // Every field except the one key must be back at the base. A field the loader carried over from the
        // scribble fails HERE -- and it fails for the WHOLE struct, not just for the handful a test remembered
        // to name, which is the only way this stays true as fields are added.
        UiConfig got = ui_config();
        CHECK(got.tgtScale >= 1.449f && got.tgtScale <= 1.451f);   // the one key the file does carry
        got.tgtScale = base.tgtScale;                              // ... neutralise it, everything else must match
        got.lang = base.lang;   // `lang` is CARRIED on purpose, not reset -- the next section is what proves it
        CHECK(ui_config_persist_eq(base, got));

        profile_delete("t_empty");
        profile_delete("t_partial");
    }

    // The two things a load must NOT reset, each for a reason (config_rules.h carries them) :
    //   - lang : a profile written before the setting existed carries no lang= line, and resetting it leaves
    //     the user reading a UI they may not understand, with the control to fix it in that same language.
    //   - favColors : the personal swatch palette. It is written into every profile file but it is NOT part of
    //     persist_eq -- the project already treats it as global -- so a load must not throw it away.
    SECTION("config : a load keeps the language and the personal palette it was not given");
    {
        char p[MAX_PATH]; plugin_path(p, sizeof(p), "data\\profiles\\t_nolang.txt");
        FILE* f = fopen(p, "w"); if (f) { fputs("tgtScale=1.2000\n", f); fclose(f); }   // no lang=, no favColors=
        profile_refresh();
        UiConfig& live = ui_config();
        live.lang = 1;                                     // FR
        live.favColorN = 2; live.favColors[0] = 0xFF112233u; live.favColors[1] = 0xFF445566u;
        CHECK(profile_load("t_nolang"));
        CHECK_EQ(ui_config().lang, 1);
        CHECK_EQ(ui_config().favColorN, 2);
        CHECK_EQ((long long)ui_config().favColors[1], (long long)0xFF445566u);
        profile_delete("t_nolang");
    }

    // ------------------------------------------------------------------------------------------------------
    // "Reset all settings" was a hand-written list of ~300 assignments. It missed exactly one field -- scTP,
    // appended to the sc2= line late -- and a hidden TP line therefore survived a full reset with nothing to
    // explain why. The defect is not the field, it is the FORM : a list maintained by hand beside 300 fields
    // will be wrong again at the next addition. This test is the invariant that makes the form safe.
    SECTION("config : Reset all settings leaves no persisted field behind");
    {
        UiConfig& live = ui_config();
        dirty_every_field(live);                           // all ~300 of them, not a hand-picked subset
        live.lang = 1;                                     // deliberately NOT reset (see config_rules.h)
        live.guideGroupCount = 2;                          // edit-mode zones : deliberately KEPT by a reset
        live.guideGroup[0] = GuideGroup(); live.guideGroup[0].x = 0.11f; live.guideGroup[0].role = 7;
        live.guideGroup[1] = GuideGroup(); live.guideGroup[1].y = 0.22f;
        const GuideGroup keptZ0 = live.guideGroup[0];

        reset_ui_config();

        UiConfig want{};                                   // the defaults, plus the two documented exceptions
        want.lang = 1;
        want.guideGroupCount = 2; want.guideGroup[0] = keptZ0;
        want.guideGroup[1] = GuideGroup(); want.guideGroup[1].y = 0.22f;
        CHECK(ui_config_persist_eq(want, ui_config()));
        CHECK_EQ(ui_config().lang, 1);                     // the language survived
        CHECK_EQ(ui_config().guideGroupCount, 2);          // and so did the zones the user drew
    }

    // ------------------------------------------------------------------------------------------------------
    // THE VALUES. mmZoom was the 2026-07-26 S0 : 1e30 out of a hand-edited file printed 35 bytes into a
    // char[16] and killed the client. It was clamped -- and it was ONE field of the hundred-odd the file
    // carries, clamped only because it had already done the damage. config_sanitise is that defence for all
    // of them, and being a pure function it can be asked directly instead of through a file.
    SECTION("config : an absurd value never reaches a draw loop");
    {
        UiConfig c{};
        float qnan; { const unsigned u = 0x7FC00000u; memcpy(&qnan, &u, sizeof(qnan)); }   // a quiet NaN
        c.text[0][0].size = 0.0f;          // "my text disappeared", and no reset explains it
        c.text[0][1].size = 1e30f;
        c.text[1][0].outline = -4.0f;
        c.tgtText[0].size = qnan;
        c.tgtScale = 0.0f; c.tmScale = 1e30f; c.dbIconScale = -2.0f;
        c.plrBarGap = -1.0f;               // a gap of 0 is legal, a negative one is not
        c.tgtBoxAlpha = 5.0f; c.mmBgAlpha = qnan; c.scBox.alpha = -3.0f;
        c.plrLum = -9.0f; c.tmBox.lum = 4.0f;
        c.mmX = -3.0f; c.tmY = 7.5f; c.hlX = qnan;
        c.tmMax = 99999; c.dbMax = -5; c.tpCount = 1000; c.buffMax = -1;
        c.mmZoom = 1e30f; c.mmRingR = -20.0f;
        c.partyRef[0] = -1.0f; c.partyBottomY = -1.0f; c.zonePanelX = -1.0f;   // -1 = "unset", not a bad value
        config_sanitise(c);

        CHECK(c.text[0][0].size >= 0.10f && c.text[0][0].size <= 4.0f);
        CHECK(c.text[0][1].size <= 4.0f);
        CHECK(c.text[1][0].outline >= 0.0f);
        CHECK(c.tgtText[0].size == c.tgtText[0].size && c.tgtText[0].size >= 0.10f);   // a NaN fails the self-compare
        CHECK(c.tgtScale >= 0.10f && c.tmScale <= 4.0f && c.dbIconScale >= 0.10f);
        CHECK(c.plrBarGap >= 0.0f);
        CHECK(c.tgtBoxAlpha <= 1.0f && c.scBox.alpha >= 0.0f);
        CHECK(c.mmBgAlpha == c.mmBgAlpha);                                             // NaN -> the default, not the floor
        CHECK(c.plrLum >= -1.0f && c.tmBox.lum <= 1.0f);
        CHECK(c.mmX >= 0.0f && c.mmX <= 1.0f && c.tmY <= 1.0f && c.hlX == c.hlX);
        CHECK(c.tmMax <= 50 && c.dbMax >= 0 && c.tpCount <= 10 && c.buffMax >= 0);
        CHECK(c.mmZoom >= 1.0f && c.mmZoom <= 24.0f && c.mmRingR >= 0.0f);
        // the unset sentinels are left exactly as they are : clamping them to [0,1] would turn every unset
        // reference line into a real one pinned at the screen edge.
        CHECK(c.partyRef[0] == -1.0f && c.partyBottomY == -1.0f && c.zonePanelX == -1.0f);
    }

    // The other half of a sanitiser, and the half that gets forgotten : it must never MOVE a value a user set.
    // The Size slider is 0.50..2.00 today, so a bound of 0.50 would silently edit a 0.45 stored by some past
    // build. These bounds are the ones no UI has ever been able to exceed, and this is the test that says so.
    SECTION("config : the sanitiser is a no-op on anything the UI can produce");
    {
        UiConfig d{}, e{};
        config_sanitise(e);
        CHECK(ui_config_persist_eq(d, e));                 // the defaults themselves are untouched

        UiConfig u{}; scribble(u, 2);                      // every field non-default, all within the UI ranges
        UiConfig v = u;
        config_sanitise(v);
        CHECK(ui_config_persist_eq(u, v));

        config_sanitise(v);                                // and it settles : sanitise(sanitise(x)) == sanitise(x)
        CHECK(ui_config_persist_eq(u, v));
    }

    profile_delete("t_roundtrip");
    profile_delete("t_roundtrip2");
}
