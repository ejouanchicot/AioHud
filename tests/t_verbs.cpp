// t_verbs.cpp -- the //aio command words (plugin/aio_verbs.h) : one table, the FIRST word matched exactly.
//
// What it locks : the dispatcher used to be ~60 `strstr(buf, "word")` tried in order, so a word ANYWHERE in the line
// fired its command -- "//aio out Poppy" changed the EmpyPop NM, "//aio in Simon" added a fake member, "//aio reset"
// ran `res`. Here every word of the table is driven with arguments built to trip exactly that : the old switches
// ("on", "off"), the old catch-alls ("hp", "why"), and every OTHER command word of the table.
#include "check.h"
#include "plugin/aio_verbs.h"
#include <stdio.h>
#include <string.h>

using namespace aio;

void test_verbs() {
    SECTION("verbs : the table -- lower case, one word, no duplicate, every command reachable");
    {
        bool seen[verbs::COUNT] = {};
        for (int i = 0; i < verbs::TABLE_N; ++i) {
            const verbs::Spec& v = verbs::TABLE[i];
            CHECK(v.id >= 0 && v.id < verbs::COUNT);
            if (v.id >= 0 && v.id < verbs::COUNT) seen[v.id] = true;
            CHECK(v.flags == verbs::SHIPPED || v.flags == verbs::DEV);
            bool clean = v.word[0] != 0;
            for (const char* c = v.word; *c; ++c) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) clean = false;
            CHECK(clean);
            for (int j = i + 1; j < verbs::TABLE_N; ++j) CHECK(strcmp(v.word, verbs::TABLE[j].word) != 0);
        }
        int unreachable = 0; for (int id = 0; id < verbs::COUNT; ++id) if (!seen[id]) ++unreachable;
        CHECK_EQ(unreachable, 0);
    }

    SECTION("verbs : NO argument fires another command -- on / off / hp / why, and every other command word");
    {
        static const char* const TRAPS[] = { "on", "off", "hp", "why", "on off", "hp 50", "why dbf", "phone", "shop",
                                             "whyrm", "onion", "offline", "reset", "poppy", "simon", "layout", "res" };
        char line[160]; int wrong = 0, total = 0;
        for (int i = 0; i < verbs::TABLE_N; ++i) {
            const verbs::Spec& v = verbs::TABLE[i];
            for (int t = 0; t < (int)(sizeof(TRAPS) / sizeof(TRAPS[0])); ++t) {
                for (int pre = 0; pre < 3; ++pre) {                       // as typed, with Windower's "aio", with slashes
                    _snprintf(line, sizeof(line), "%s%s %s", pre == 0 ? "" : pre == 1 ? "aio " : "//aio ", v.word, TRAPS[t]);
                    line[sizeof(line) - 1] = 0;
                    const char* rest = 0;
                    ++total; if (verbs::find(line, &rest) != v.id) { ++wrong; printf("    wrong : \"%s\"\n", line); }
                }
            }
            for (int j = 0; j < verbs::TABLE_N; ++j) {                    // ...and every other command word as argument
                _snprintf(line, sizeof(line), "%s %s", v.word, verbs::TABLE[j].word); line[sizeof(line) - 1] = 0;
                ++total; if (verbs::find(line, 0) != v.id) { ++wrong; printf("    wrong : \"%s\"\n", line); }
            }
        }
        CHECK_EQ(wrong, 0);
        CHECK(total > 3000);
    }

    SECTION("verbs : the cases the old chain got wrong, by name");
    {
        const char* rest = 0;
        CHECK_EQ(verbs::find("out poppy", &rest), (int)verbs::V_OUT);       // was : EmpyPop ("pop")
        CHECK_STR(rest, " poppy");
        CHECK_EQ(verbs::find("in simon", 0), (int)verbs::V_IN);            // was : the fake party member ("sim")
        CHECK_EQ(verbs::find("reset", 0), (int)verbs::NONE);             // was : `res`
        CHECK_EQ(verbs::find("layouts", 0), (int)verbs::NONE);
        CHECK_EQ(verbs::find("lay 3", 0), (int)verbs::V_LAY);              // and its neighbour stays itself
        CHECK_EQ(verbs::find("layout", 0), (int)verbs::V_LAYOUT);
        CHECK_EQ(verbs::find("outside", 0), (int)verbs::NONE);
        CHECK_EQ(verbs::find("on", 0), (int)verbs::NONE);
        CHECK_EQ(verbs::find("hp 50", 0), (int)verbs::NONE);             // the dev fill catch-all is NOT a table word
        CHECK_EQ(verbs::find("why", 0), (int)verbs::V_WHY);
        CHECK_EQ(verbs::find("pop whyrm", 0), (int)verbs::V_EPT);
        CHECK_EQ(verbs::find("chat export all", &rest), (int)verbs::V_CHAT);
        CHECK_STR(rest, " export all");
        CHECK_EQ(verbs::find("alliance1 demo", 0), (int)verbs::V_PARTY);   // the legacy spellings keep working
        CHECK_EQ(verbs::find("demo off", 0), (int)verbs::V_PARTY);
        CHECK_EQ(verbs::find("aio", 0), (int)verbs::NONE);
        CHECK_EQ(verbs::find("", &rest), (int)verbs::NONE);
        CHECK(rest != 0);                                                // never null, even for nothing
        CHECK_EQ(verbs::find("   //aio   config 2", &rest), (int)verbs::V_CONFIG);
        CHECK_STR(rest, " 2");
    }

    SECTION("verbs : R12 -- a line past 255 characters is cut, and says how much it lost");
    {
        char in[400], out[256];
        for (int i = 0; i < 300; ++i) in[i] = (char)('A' + i % 26);
        in[300] = 0;
        CHECK_EQ(verbs::copy_lower(in, out, sizeof(out)), 300 - 255);
        CHECK_EQ((int)strlen(out), 255);
        CHECK_EQ(out[0], 'a');                                          // lower-cased on the way
        in[255] = 0;
        CHECK_EQ(verbs::copy_lower(in, out, sizeof(out)), 0);            // exactly 255 : whole
        CHECK_EQ(verbs::copy_lower("Doctor", out, sizeof(out)), 0);
        CHECK_STR(out, "doctor");
    }
}
