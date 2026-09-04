// t_buffgroups.cpp -- status id -> display group, the classification behind the party strip's configurable order.
//
// WHY THIS EXISTS. buff_groups.h resolves a status through THREE layers -- the generated BUFF_FAM table, the
// debuff list, and a hand-written table of exceptions -- and the exceptions are not decoration. BUFF_FAM is
// keyed by (status, SOURCE), so 22 statuses appear under two or three categories and the row listed FIRST is
// routinely not the one a player means:
//     71 is "Monomi: Ichi" (Ninjutsu) before it is "Sneak"
//     69 is "Tonko: Ni"    (Ninjutsu) before it is "Invisible"
// Drop the exception table, or let the generator reorder BUFF_FAM, and Sneak/Invisible quietly move to the
// Abilities block -- the exact buffs this feature was asked for, silently in the wrong place. Nothing about the
// HUD would look broken; the icons would just not be where they were put. So the trap is pinned here by name.
//
// NOT tested here: that the grouping is the RIGHT taste (it is the user's), or the strip's geometry (that needs
// a device). This asks one question -- does a status land in the group the design says it does.
#include "check.h"
#include "model/buff_groups.h"
#include "model/ui_config.h"
#include "ui/party_demo_buffs.h"   // BUFF_POOL : what the config preview actually renders

using namespace aio;

void test_buff_groups() {
    SECTION("buff groups : the statuses BUFF_FAM files under the wrong first row");
    // The whole reason BUFF_GROUP_FIX exists. Each of these has a Ninjutsu / blood-pact / heal row ahead of the
    // one a player thinks of, so "first row wins" puts them somewhere else entirely.
    CHECK(buff_group(71) == BG_STEALTH);    // Sneak, not "Monomi: Ichi"
    CHECK(buff_group(69) == BG_STEALTH);    // Invisible, not "Tonko: Ni"
    CHECK(buff_group(70) == BG_STEALTH);    // Deodorize
    CHECK(buff_group(113) == BG_PROTECT);   // Reraise, which BUFF_FAM files under TC_HEAL
    CHECK(buff_group(116) == BG_WATCH);     // Phalanx, which BUFF_FAM files under TC_DEFENSE with Blink/Stoneskin
    CHECK(buff_group(33) == BG_WATCH);      // Haste, also listed as Refueling (BLU) and Hastega (SMN)
    CHECK(buff_group(42) == BG_WATCH);      // Regen, also listed as Regeneration (BLU)

    SECTION("buff groups : the blocks a player reads as one thing");
    CHECK(buff_group(43) == BG_WATCH);      // Refresh
    CHECK(buff_group(581) == BG_WATCH);     // Flurry
    CHECK(buff_group(40) == BG_PROTECT);    // Protect
    CHECK(buff_group(41) == BG_PROTECT);    // Shell
    CHECK(buff_group(36) == BG_PROTECT);    // Blink
    CHECK(buff_group(37) == BG_PROTECT);    // Stoneskin
    CHECK(buff_group(39) == BG_PROTECT);    // Aquaveil

    SECTION("buff groups : the generated categories still carry the rest");
    // Not exceptions -- these come straight from BUFF_FAM, and they are what makes the exception table small.
    CHECK(buff_group(214) == BG_SONG);      // March (every March collapses onto status 214)
    CHECK(buff_group(198) == BG_SONG);      // Minuet
    CHECK(buff_group(313) == BG_ROLL);      // Chaos Roll
    CHECK(buff_group(541) == BG_GEO);       // Indi-/Geo-Refresh : a GEO aura stays with the auras, not with Refresh
    CHECK(buff_group(523) == BG_RUNE);      // Ignis
    CHECK(buff_group(535) == BG_RUNE);      // Valiance (a ward, same block as the runes)
    CHECK(buff_group(370) == BG_DANCE);     // Haste Samba
    CHECK(buff_group(251) == BG_PERM);      // Food
    CHECK(buff_group(253) == BG_PERM);      // Signet

    SECTION("buff groups : what neither table knows");
    CHECK(buff_group(2) == BG_DEBUFF);      // Sleep
    CHECK(buff_group(3) == BG_DEBUFF);      // Poison
    CHECK(buff_group(13) == BG_DEBUFF);     // Slow
    CHECK(buff_group(134) == BG_DEBUFF);    // Dia
    CHECK(buff_group(639) == BG_OTHER);     // last atlas cell, named by nothing
    CHECK(buff_group(9999) == BG_OTHER);    // past the table entirely -> must answer, not read out of bounds

    SECTION("buff groups : every id resolves to a real group");
    // The strip indexes a per-group array with whatever this returns, for up to 32 ids per member per frame.
    // One out-of-range answer is an out-of-bounds write in the sort, so check the WHOLE domain, not samples.
    bool allInRange = true;
    for (unsigned id = 0; id < 700; ++id) if (buff_group(id) >= BG_COUNT) allInRange = false;
    CHECK(allInRange);

    // Every group must be reachable, or a row in the config list orders something that can never appear.
    bool used[BG_COUNT] = { false };
    for (unsigned id = 0; id < (unsigned)BUFF_GROUP_MAX_ID; ++id) used[buff_group(id)] = true;
    bool allUsed = true;
    for (int g = 0; g < BG_COUNT; ++g) if (!used[g]) allUsed = false;
    CHECK(allUsed);

    SECTION("buff groups : the stated order INSIDE a group");
    // These sequences exist nowhere in the game data -- the 0x076 list is acquisition order, so "Sneak then
    // Invisible" is never what arrives. They are a stated decision, and the ONLY thing holding them is the
    // line order of BUFF_GROUP_FIX. Swap two lines there and the HUD changes with nothing else complaining,
    // so the sequences are pinned here by name rather than left to a reader to notice.
    CHECK(buff_group_pri(71) < buff_group_pri(69));    // Sneak before Invisible
    CHECK(buff_group_pri(69) < buff_group_pri(70));    // Invisible before Deodorize
    CHECK(buff_group_pri(40) < buff_group_pri(41));    // Protect before Shell
    CHECK(buff_group_pri(33) < buff_group_pri(43));    // Haste before Refresh
    CHECK(buff_group_pri(43) < buff_group_pri(116));   // Refresh before Phalanx
    // ... and those three come before EVERY other status in Watch, whatever else lands there. Checking the
    // three pairs above would not say that : it would still pass if Regen were ranked ahead of Haste, and it
    // would say nothing at all about a status a future BUFF_FAM adds to the group. So state the real rule and
    // sweep the whole group for it.
    {
        const unsigned char pHaste = buff_group_pri(33), pRef = buff_group_pri(43), pPhal = buff_group_pri(116);
        unsigned char worst = pHaste; if (pRef > worst) worst = pRef; if (pPhal > worst) worst = pPhal;
        bool threeFirst = true; int others = 0;
        for (unsigned id = 0; id < (unsigned)BUFF_GROUP_MAX_ID; ++id) {
            if (buff_group(id) != BG_WATCH) continue;
            if (id == 33 || id == 43 || id == 116) continue;
            ++others;
            if (buff_group_pri(id) <= worst) { threeFirst = false; printf("   Watch status %u outranks Haste/Refresh/Phalanx\n", id); }
        }
        CHECK(threeFirst);
        CHECK(others > 0);   // a sweep that found nothing to compare against proves nothing -- Regen and Flurry are in here
    }
    // A status nobody ranked sorts AFTER every ranked one in its group, and keeps the game's order among its
    // own kind (the sort is stable). 255 is that "unranked" marker -- it must never collide with a real rank.
    CHECK(buff_group_pri(214) == 0xFF);                // a March : never ranked, so game order
    CHECK(buff_group_pri(116) < 0xFF);                 // Phalanx IS ranked
    CHECK(buff_group_pri(9999) == 0xFF);               // out of range -> must answer, not read out of bounds

    SECTION("buff groups : the order the USER arranged wins over the built-in one");
    {
        UiConfig& c = ui_config();
        for (int g = 0; g < UiConfig::BUFF_ORDER_N; ++g) c.buffPinN[g] = 0;
        CHECK(buff_pri_effective(c, 33) < buff_pri_effective(c, 43));   // no arrangement -> the built-in list still decides

        // Arrange Watch the other way round : Refresh first, then Haste.
        c.buffPin[BG_WATCH][0] = 43; c.buffPin[BG_WATCH][1] = 33; c.buffPinN[BG_WATCH] = 2;
        CHECK(buff_pri_effective(c, 43) < buff_pri_effective(c, 33));
        // An arranged group ignores the built-in ranks ENTIRELY -- Phalanx is ranked by default, but it is
        // not in this prefix, so it now sits with the unarranged ones. (The UI seeds the prefix from what is
        // on screen, so a real user never sees this jump ; the rule still has to be exactly this one.)
        CHECK(buff_pri_effective(c, 116) == 0xFF);
        CHECK(buff_pri_effective(c, 40) < 0xFF);   // a DIFFERENT group is untouched : Protect keeps its default rank

        SECTION("buff groups : listing a group's members, in draw order");
        unsigned short mem[UiConfig::BUFF_PIN_MAX]; int total = 0;
        // seen = null : the CURATED list only. This is the state at first launch, and the groups that matter
        // must already be complete there -- Watch shows its five whether or not anything has turned up yet.
        int n = buff_group_members(c, BG_WATCH, mem, UiConfig::BUFF_PIN_MAX, &total, 0);
        CHECK(n == 5);
        CHECK(mem[0] == 43);   // the arrangement drives the list the config shows
        CHECK(mem[1] == 33);
        CHECK(total >= n);

        // THE CAP MUST DROP THE WORST RANK, NOT THE HIGHEST ID. A status the user pinned to position 1 can
        // have any id at all ; a cap that kept "the first `cap` ids" would evict it and keep unarranged ones
        // instead -- the arrangement would silently vanish from the list that is supposed to show it.
        c.buffPin[BG_JA][0] = 500; c.buffPinN[BG_JA] = 1;            // Overkill : a high id, pinned first
        CHECK(buff_group(500) == BG_JA);
        unsigned short few[5]; int jaTotal = 0;
        // "everything has been seen" -> the group is at full size, which is the only state where the cap bites.
        const int nf = buff_group_members(c, BG_JA, few, 5, &jaTotal, [](unsigned) { return true; });
        CHECK(nf == 5);
        CHECK(jaTotal > 5);        // the group really is bigger than the cap, or this proves nothing
        CHECK(few[0] == 500);      // ... and the pinned one survived the cut

        for (int g = 0; g < UiConfig::BUFF_ORDER_N; ++g) c.buffPinN[g] = 0;   // leave the singleton as we found it

        SECTION("buff groups : an open-ended group lists what you MET, not what it could hold");
        // The whole point of the `seen` filter. "Other" can hold ~238 named statuses ; offering all of them to
        // be hand-ordered is the menu this replaced. Curated groups are unaffected -- they are already listed.
        int otherAll = 0, otherCurated = 0;
        unsigned short buf[UiConfig::BUFF_PIN_MAX];
        buff_group_members(c, BG_OTHER, buf, UiConfig::BUFF_PIN_MAX, &otherAll, [](unsigned) { return true; });
        buff_group_members(c, BG_OTHER, buf, UiConfig::BUFF_PIN_MAX, &otherCurated, 0);
        CHECK(otherAll > 100);      // the universe really is that big, or this test proves nothing
        CHECK(otherCurated == 0);   // ... and none of it is offered until it actually turns up
        // A status becomes offerable the moment it is seen, and only that one.
        int otherOne = 0;
        const int nOne = buff_group_members(c, BG_OTHER, buf, UiConfig::BUFF_PIN_MAX, &otherOne,
                                            [](unsigned st) { return st == 252u; });   // "Mounted"
        CHECK(buff_group(252) == BG_OTHER);
        CHECK(nOne == 1);
        CHECK(buf[0] == 252);

        // The config's "All" mode reads a group into a fixed 256-slot buffer. No group may outgrow it, or the
        // list would silently stop short of statuses the mode exists to reach. Biggest group today: Other.
        int biggest = 0;
        for (int g = 0; g < BG_COUNT; ++g) {
            int t = 0; unsigned short tmp[256];
            buff_group_members(c, g, tmp, 256, &t, [](unsigned) { return true; });
            if (t > biggest) biggest = t;
        }
        CHECK(biggest <= 256);
    }

    SECTION("buff groups : the config preview can demonstrate every group");
    // The preview draws the demo pool through the SAME sort as the live HUD. A group with no status in the
    // pool is a row in the "Buff order" list that the user can move up and down while NOTHING on screen
    // changes -- the setting reads as broken when it is the sample that is empty. This is not hypothetical:
    // the original pool had no Sneak / Invisible / Deodorize at all, i.e. the one group the ordering feature
    // was asked for. Whoever edits BUFF_POOL next finds out here instead of in game.
    {
        bool covered[BG_COUNT] = { false };
        for (int i = 0; i < BUFF_POOL_N; ++i) covered[buff_group(BUFF_POOL[i])] = true;
        for (int g = 0; g < BG_COUNT; ++g) {
            if (!covered[g]) printf("   group with no sample in BUFF_POOL: %s\n", BUFF_GROUP_EN[g]);
            CHECK(covered[g]);
        }
    }
}
