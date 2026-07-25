// t_json.cpp -- layout.json parser / serialiser.
//
// This file exists because BOTH blocking defects found by the 2026-07-25 audit were here, and both are
// reachable from a FILE the plugin reads at start-up, outside any __try :
//   * num_str formatted an out-of-range double straight into a char[40] -- 1e300 renders as 301 digits.
//   * parse_value <-> parse_object/parse_array had no depth counter -- "[[[[..." ran the stack out.
// They were fixed in ef17da7 with nothing to keep them fixed. That is what these checks are.
#include "check.h"
#include "io/json.h"
#include <string>

using namespace aio;

static std::string rt(const char* src) {          // parse then re-serialise
    json::Value v;
    if (!json::parse(src, v)) return "<parse-failed>";
    return json::dump(v);
}

// NEVER write CHECK_STR(rt(x).c_str(), ...) : the temporary std::string dies at the end of the declaration
// inside the macro, and the stored pointer dangles. It even passes sometimes, because a short result lives in
// the string's inline buffer and the freed stack slot often still holds it. Bind the result first.
#define CHECK_RT(src, expected) do { const std::string _s = rt(src); CHECK_STR(_s.c_str(), expected); } while (0)

void test_json() {
    SECTION("json : round-trip (no regression on the normal path)");
    CHECK_RT("{\"a\":36.5}",   "{\n  \"a\": 36.5\n}\n");
    CHECK_RT("{\"a\":100}",    "{\n  \"a\": 100\n}\n");     // integers stay integers
    CHECK_RT("{\"a\":0.25}",   "{\n  \"a\": 0.25\n}\n");
    CHECK_RT("{\"a\":-12.75}", "{\n  \"a\": -12.75\n}\n");
    CHECK_RT("[]",             "[]\n");
    CHECK_RT("{}",             "{}\n");
    CHECK_RT("{\"s\":\"x\\ny\"}", "{\n  \"s\": \"x\\ny\"\n}\n");   // escapes survive

    SECTION("json : an out-of-range number cannot overflow the format buffer");
    // Every number read from layout.json goes back through num_str on the next save, so a corrupt or
    // hand-edited file is the input here. The clamp is +-1e15, the same window the integer path already used.
    CHECK(rt("{\"x\":1e300}").size()  < 64);
    CHECK(rt("{\"x\":-1e300}").size() < 64);
    CHECK(rt("{\"x\":1.7976931348623157e308}").size() < 64);
    CHECK(rt("{\"x\":999999999999999.5}").size() < 64);   // worst case just under the clamp
    CHECK_RT("{\"x\":0.0}",  "{\n  \"x\": 0\n}\n");       // 0.0 must not take the clamp path
    CHECK_RT("{\"x\":-0.0}", "{\n  \"x\": 0\n}\n");       // nor -0.0 (every comparison against it is false)

    SECTION("json : nesting is bounded (startup crash, outside any __try)");
    {
        std::string open200(200, '[');
        json::Value v;
        CHECK(!json::parse(open200, v));                  // unterminated AND too deep -> refused, not a crash

        std::string deep;                                 // 5000 balanced levels
        deep.append(5000, '['); deep.append(5000, ']');
        json::Value v2;
        CHECK(!json::parse(deep, v2));

        std::string objdeep;                              // objects recurse through the same path
        for (int i = 0; i < 300; ++i) objdeep += "{\"a\":";
        json::Value v3;
        CHECK(!json::parse(objdeep, v3));
    }

    SECTION("json : legitimate depth still parses");
    {
        json::Value v;                                    // root > widgets > widget > config > object = 5
        CHECK(json::parse("{\"widgets\":[{\"config\":{\"a\":{\"b\":1}}}]}", v));
        std::string ok60;                                 // 60 levels, just under the 64 limit
        ok60.append(60, '['); ok60.append(60, ']');
        json::Value v2;
        CHECK(json::parse(ok60, v2));
    }

    SECTION("json : malformed input is refused, never accepted half-way");
    {
        json::Value v;
        CHECK(!json::parse("", v));
        CHECK(!json::parse("{", v));
        CHECK(!json::parse("{\"a\":}", v));
        CHECK(!json::parse("[1,2", v));
        CHECK(!json::parse("{\"a\":\"unterminated", v));
        CHECK(!json::parse("{\"a\":\"\\u00\"}", v));      // truncated \u escape at the buffer edge
    }
}
