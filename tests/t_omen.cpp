// t_omen.cpp -- the Omen bonus-objective state machine, replayed from a REAL capture.
//
// Every line below was copied out of aiohud_debug.log (//aio omenparse, 2026-07-29, Reisenjima Henge), not
// invented. That matters twice over: the wording is the game's, down to the singular/plural swaps that broke two
// separate rules, and the SEQUENCE is a real run rather than the one a test author would think to write.
//
// The bug this exists for: objectives are per floor, and the count CHANGES per floor. The reset used to hang off
// "A spectral light flares up.", which the game only prints when you fully clear a floor -- so on any floor left
// unfinished the ten slots were never cleared, only overwritten. Ten objectives followed by three left slots
// 4-10 displaying the PREVIOUS floor's objectives, frozen mid-progress, unable to ever validate while the chat
// reported the current floor's completions. Reaching that state in game costs an Omen run; here it costs a ms.
#include "check.h"
#include "model/omen_objectives.h"

using namespace aio;

// The game terminates every Omen line with the marker byte 0x7F followed by one code byte (0x31 = '1'). Written
// as two adjacent literals on purpose: "\x7F1" would be parsed as ONE hex escape (\x7F1), not 0x7F then '1'.
#define OM(text) text "\x7F" "1"

// Drive a line exactly the way the plugin does: ONE call, wipe decision included. The test must never reset the
// array itself -- that would prove the sequence and quietly assume the very coupling that was broken.
static bool g_cleared = false;                    // the "A spectral light flares up." latch the plugin keeps
static void feed(OmenObj* o, const char* line) {
    if (omen_feed_slots(o, line, g_cleared)) g_cleared = false;
    if (line && strstr(line, "spectral light flares up")) g_cleared = true;
}

void test_omen() {
    SECTION("omen : each objective line yields its type, its count and its target");

    OmenObj o[10];
    omen_reset(o);

    // --- Floor A, exactly as captured: three objectives. -----------------------------------------------------
    feed(o, OM("Follow the light and carry out your charge before time runs out.You have 180 seconds remaining."));
    feed(o, OM("1: Cast 6 spells on your foes."));
    feed(o, OM("2: Use 10 weapon skills on your foes."));
    feed(o, OM("3: Reduce your foe's HP by at least 15000 using a single magic attack without performing a magic burst."));

    CHECK_EQ(o[0].type, 8);   CHECK_EQ(o[0].req, 6);       CHECK_EQ(o[0].cur, 0);   // Spells
    CHECK_EQ(o[1].type, 11);  CHECK_EQ(o[1].req, 10);      CHECK_EQ(o[1].cur, 0);   // All WS
    CHECK_EQ(o[2].type, 3);   CHECK_EQ(o[2].req, 15000);   CHECK_EQ(o[2].cur, 0);   // Non-MB Nuke
    CHECK_EQ(o[3].type, 0);                                                          // nothing beyond slot 3
    CHECK_STR(omen_short_name(o[0].type), "Spells");

    // Progress lines. SINGULAR at one ("1 spell", "1 weapon skill") -- the game switches, and a rule keyed on the
    // plural would stop counting at exactly the value everyone stares at.
    feed(o, OM("2: You have used 1 weapon skill on your foes."));
    CHECK_EQ(o[1].cur, 1);
    feed(o, OM("1: You have cast 1 spell on your foes."));
    CHECK_EQ(o[0].cur, 1);
    feed(o, OM("1: You have cast 5 spells on your foes."));
    CHECK_EQ(o[0].cur, 5);
    CHECK(!omen_done(o[0]));                                  // 5 of 6 -- not yet
    feed(o, OM("1: You have cast 6 spells on your foes."));
    CHECK(omen_done(o[0]));

    // A progress line must never move the TARGET. The statement can be re-broadcast mid-floor at any time and
    // must not reset the count either -- captured three times on this one floor.
    feed(o, OM("1: Cast 6 spells on your foes."));
    CHECK_EQ(o[0].req, 6); CHECK_EQ(o[0].cur, 6);

    SECTION("omen : a damage objective reports the damage DEALT, not the target");

    // The trap that would declare every damage objective complete on the first hit: this line's number is 455,
    // which is what the auto-attack did -- the requirement is still 2000.
    omen_reset(o);
    feed(o, OM("7: Reduce your foe's HP by at least 2000 in a single auto-attack."));
    CHECK_EQ(o[6].type, 4); CHECK_EQ(o[6].req, 2000);
    feed(o, OM("7: You have reduced your foe's HP by 455 in a single auto-attack."));
    CHECK_EQ(o[6].cur, 455); CHECK_EQ(o[6].req, 2000);
    CHECK(!omen_done(o[6]));
    feed(o, OM("7: You have reduced your foe's HP by 1750 in a single auto-attack."));
    CHECK_EQ(o[6].cur, 1750); CHECK(!omen_done(o[6]));

    SECTION("omen : a damage objective keeps its BEST attempt, it never walks backwards");

    // The game reports EVERY attempt, not the best so far -- slot 7 of the 2026-07-30 run went 659, 998, 1055,
    // 1175, 1703 through five separate auto-attacks. That capture only ever climbed, so assigning the latest
    // looked correct; the very next weaker hit would have dragged the bar back down in front of the player.
    omen_reset(o);
    feed(o, OM("7: Reduce your foe's HP by at least 2000 in a single auto-attack."));
    feed(o, OM("7: You have reduced your foe's HP by 1703 in a single auto-attack."));
    CHECK_EQ(o[6].cur, 1703);
    feed(o, OM("7: You have reduced your foe's HP by 659 in a single auto-attack."));
    CHECK_EQ(o[6].cur, 1703);                                  // the weak hit is not progress
    feed(o, OM("7: You have reduced your foe's HP by 2100 in a single auto-attack."));
    CHECK_EQ(o[6].cur, 2100); CHECK(omen_done(o[6]));           // a better one still is

    // A COUNTING objective is the opposite case: its number is the running total the server keeps, so it is
    // taken exactly as stated -- including a jump, which an AoE ability counting per foe really produced
    // (slot 3 went straight from 1 to 4, with no line in between, in the same run).
    omen_reset(o);
    feed(o, OM("3: Use 4 abilities on your foes."));
    feed(o, OM("3: You have used 1 ability on your foes."));    CHECK_EQ(o[2].cur, 1);
    feed(o, OM("3: You have used 4 abilities on your foes."));
    CHECK_EQ(o[2].cur, 4); CHECK(omen_done(o[2]));

    // And a damage slot REPLACED by another objective starts from zero -- the best-attempt rule must not carry
    // a stale high value across.
    omen_reset(o);
    feed(o, OM("7: Reduce your foe's HP by at least 2000 in a single auto-attack."));
    feed(o, OM("7: You have reduced your foe's HP by 1900 in a single auto-attack."));
    feed(o, OM("7: Reduce your foe's HP by at least 30000 using a single weapon skill."));
    CHECK_EQ(o[6].type, 1); CHECK_EQ(o[6].cur, 0); CHECK_EQ(o[6].req, 30000);
    feed(o, OM("7: You have reduced your foe's HP by 800 using a single weapon skill."));
    CHECK_EQ(o[6].cur, 800);

    SECTION("omen : all ten objective types of the captured floor classify correctly");

    // Floor B, exactly as captured: ten objectives, announced right after the 600 s window opened.
    feed(o, OM("Follow the light and carry out your charge before time runs out.You have 600 seconds remaining."));
    feed(o, OM("1: Execute 2 skillchains using weapon skills on your foes!"));
    feed(o, OM("2: Deal 6 critical hits to your foes."));
    feed(o, OM("3: Vanquish 2 foes."));
    feed(o, OM("4: Use 6 elemental weapon skills on your foes."));
    feed(o, OM("5: Use 10 weapon skills on your foes."));
    feed(o, OM("6: Perform 6 magic bursts on your foes."));
    feed(o, OM("7: Reduce your foe's HP by at least 2000 in a single auto-attack."));
    feed(o, OM("8: Reduce your foe's HP by at least 15000 using a single magic attack without performing a magic burst."));
    feed(o, OM("9: Reduce your foe's HP by at least 30000 using a single magic burst."));
    feed(o, OM("10: Restore at least 500 HP 10 times."));

    // The ordering hazards live here: "Execute 2 skillchains using weapon skills" must NOT read as All WS, and
    // "using a single magic attack without performing a magic burst" must NOT read as Magic Bursts.
    CHECK_EQ(o[0].type, 10); CHECK_EQ(o[0].req, 2);        // Skillchains, not All WS
    CHECK_EQ(o[1].type, 6);  CHECK_EQ(o[1].req, 6);        // Critical Hits
    CHECK_EQ(o[2].type, 5);  CHECK_EQ(o[2].req, 2);        // Kills
    CHECK_EQ(o[3].type, 13); CHECK_EQ(o[3].req, 6);        // Magic WS, not All WS
    CHECK_EQ(o[4].type, 11); CHECK_EQ(o[4].req, 10);       // All WS
    CHECK_EQ(o[5].type, 9);  CHECK_EQ(o[5].req, 6);        // Magic Bursts
    CHECK_EQ(o[6].type, 4);  CHECK_EQ(o[6].req, 2000);     // Melee Round
    CHECK_EQ(o[7].type, 3);  CHECK_EQ(o[7].req, 15000);    // Non-MB Nuke, not Magic Bursts
    CHECK_EQ(o[8].type, 2);  CHECK_EQ(o[8].req, 30000);    // MB Damage
    CHECK_EQ(o[9].type, 14); CHECK_EQ(o[9].req, 10);       // 500 HP Cures -- the 10 of "10 times", not the 500

    // Their progress lines, singular forms included.
    feed(o, OM("1: You have executed 1 skillchain using weapon skills on your foes!"));   CHECK_EQ(o[0].cur, 1);
    feed(o, OM("2: You have dealt 1 critical hit to your foes."));                        CHECK_EQ(o[1].cur, 1);
    feed(o, OM("4: You have used 1 elemental weapon skill on your foes."));               CHECK_EQ(o[3].cur, 1);
    feed(o, OM("10: You have restored at least 500 HP 1 time."));                         CHECK_EQ(o[9].cur, 1);
    feed(o, OM("2: You have dealt 6 critical hits to your foes."));
    CHECK_EQ(o[1].cur, 6); CHECK(omen_done(o[1]));

    SECTION("omen : the periodic re-announce lists only the REMAINING objectives");

    // Captured verbatim: with 1-5 finished the game re-broadcast slots 6, 8 and 10 alone. The silent ones must
    // keep their state -- a reader that treated the short list as "the floor's objectives" would erase them.
    feed(o, OM("6: Perform 6 magic bursts on your foes."));
    feed(o, OM("8: Reduce your foe's HP by at least 15000 using a single magic attack without performing a magic burst."));
    feed(o, OM("10: Restore at least 500 HP 10 times."));
    CHECK_EQ(o[1].cur, 6);  CHECK(omen_done(o[1]));        // slot 2 untouched by the partial list
    CHECK_EQ(o[9].cur, 1);  CHECK_EQ(o[9].req, 10);        // slot 10 keeps its count, target restated

    SECTION("omen : THE BUG -- ten objectives then three must not leave seven stale rows");

    // The regression this whole file exists for. Floor B (ten) is live above; floor C lists three. Before the
    // fix nothing cleared slots 4-10, so they kept floor B's objectives -- visible, frozen, never validating.
    feed(o, OM("Follow the light and carry out your charge before time runs out.You have 180 seconds remaining."));
    feed(o, OM("1: Cast 6 spells on your foes."));
    feed(o, OM("2: Use 10 weapon skills on your foes."));
    feed(o, OM("3: Reduce your foe's HP by at least 15000 using a single magic attack without performing a magic burst."));
    for (int i = 3; i < 10; ++i) { CHECK_EQ(o[i].type, 0); CHECK_EQ(o[i].cur, 0); CHECK_EQ(o[i].req, 0); }
    CHECK_EQ(o[0].type, 8);                                 // and floor C's own three are intact
    CHECK_EQ(o[1].type, 11);
    CHECK_EQ(o[2].type, 3);

    SECTION("omen : only the window OPENING is a new floor -- ticks and flavour lines are not");

    // Every one of these was captured during a single floor. If any counted as a new floor, the fix would wipe
    // live progress mid-run, which is a worse bug than the one it replaces.
    CHECK(!omen_is_new_floor_line(OM("Carry out your charge before time runs out.You have 60 seconds remaining.")));
    CHECK(!omen_is_new_floor_line(OM("Carry out your charge before time runs out.You have 5 seconds remaining.")));
    CHECK(!omen_is_new_floor_line(OM("Obey... Follow the light...")));          // shares the first three words
    CHECK(!omen_is_new_floor_line(OM("Vanquish all transcended foes.")));       // the floor objective, re-broadcast
    CHECK(!omen_is_new_floor_line(OM("There are 827 omens from your foes!")));
    CHECK(!omen_is_new_floor_line(OM("A spectral light flares up.")));

    // ...and all of the ticks are still TIMER lines, including the singular last one that used to be dropped.
    CHECK(omen_is_timer_line(OM("Carry out your charge before time runs out.You have 60 seconds remaining.")));
    CHECK(omen_is_timer_line(OM("Carry out your charge before time runs out.You have 1 second remaining.")));
    CHECK_EQ(omen_first_num(OM("Carry out your charge before time runs out.You have 1 second remaining.")), 1);
    CHECK(!omen_is_timer_line(OM("Obey... Follow the light...")));

    SECTION("omen : failure lines are ignored, not counted");

    // "You have failed to perform 6 magic bursts" carries a 6 and the word "You have". Read as progress it would
    // mark the objective COMPLETE at the exact moment the game said it was lost.
    omen_reset(o);
    feed(o, OM("6: Perform 6 magic bursts on your foes."));
    feed(o, OM("6: You have failed to perform 6 magic bursts on your foes."));
    CHECK(omen_parse_line(OM("6: You have failed to perform 6 magic bursts on your foes.")).fail);
    CHECK_EQ(o[5].cur, 0);
    CHECK(!omen_done(o[5]));

    SECTION("omen : joining mid-floor reports an UNKNOWN target, never a reached one");

    omen_reset(o);
    feed(o, OM("2: You have used 4 weapon skills on your foes."));     // no statement seen for this slot
    CHECK_EQ(o[1].type, 11);
    CHECK_EQ(o[1].cur, 4);
    CHECK_EQ(o[1].req, -1);                                  // -> the box prints "???"
    CHECK(!omen_done(o[1]));                                 // and cannot claim completion from a target it lacks
    feed(o, OM("2: Use 10 weapon skills on your foes."));     // the next re-announce fills it in
    CHECK_EQ(o[1].req, 10); CHECK_EQ(o[1].cur, 4);

    SECTION("omen : the floor banner drops the 0x7F marker AND its code byte");

    char buf[48];
    omen_clean_text(OM("Vanquish all monsters."), buf, sizeof(buf));
    CHECK_STR(buf, "Vanquish all monsters.");                 // was "Vanquish all monsters.1"
    omen_clean_text(OM("Vanquish all transcended foes."), buf, sizeof(buf));
    CHECK_STR(buf, "Vanquish all transcended foes.");
    omen_clean_text("Free Floor!", buf, sizeof(buf));
    CHECK_STR(buf, "Free Floor!");
    omen_clean_text(0, buf, sizeof(buf));
    CHECK_STR(buf, "");

    SECTION("omen : boss and treasure floors carry no bonus objectives");

    // Ported from the reference addon's hide list, which we had not implemented. On these floors the box must not
    // announce a bonus window at all -- and before the wipe fix it showed the PREVIOUS floor's rows there.
    CHECK(!omen_floor_has_bonus("Vanquish Kin."));
    CHECK(!omen_floor_has_bonus("Vanquish Ou."));
    CHECK(!omen_floor_has_bonus("Vanquish Kyou."));
    CHECK(!omen_floor_has_bonus("Vanquish the Craver."));
    CHECK(!omen_floor_has_bonus("Vanquish the Gorger."));
    CHECK(!omen_floor_has_bonus("Vanquish the Thinker."));
    CHECK(!omen_floor_has_bonus("Open 3 Treasure portents"));
    CHECK(!omen_floor_has_bonus("Waiting for objectives..."));
    CHECK(!omen_floor_has_bonus(""));                          // nothing known -> promise nothing

    // ...and an ordinary floor still does. The addon matched these tokens as bare substrings; "Fu" and "Ou" are
    // two letters, so that rule blanks a normal floor the day a monster name happens to contain them. Whole-word
    // matching is what keeps these three honest.
    CHECK(omen_floor_has_bonus("Vanquish all monsters."));
    CHECK(omen_floor_has_bonus("Vanquish all transcended foes."));
    CHECK(omen_floor_has_bonus("Vanquish all Fungars."));       // contains "Fu"
    CHECK(omen_floor_has_bonus("Vanquish all Ouryu."));         // contains "Ou"
    CHECK(omen_floor_has_bonus("Vanquish all Kindred."));       // contains "Kin"
    CHECK(omen_floor_has_bonus("Free Floor!"));

    SECTION("omen : lines that are not objectives are left alone");

    CHECK_EQ(omen_parse_line(OM("There are 827 omens from your foes!")).slot, 0);
    CHECK_EQ(omen_parse_line(OM("Obey... Follow the light...")).slot, 0);
    CHECK_EQ(omen_parse_line("11: something with too high a slot").slot, 0);
    CHECK_EQ(omen_parse_line("").slot, 0);
    CHECK_EQ(omen_parse_line(0).slot, 0);
    CHECK_EQ(omen_parse_line(OM("3: an objective with no recognisable pattern")).type, 0);
}
