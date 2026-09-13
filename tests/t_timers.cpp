// t_timers.cpp -- the Timers box rows, decided by the REAL model and the REAL builder in a fake game (fake_game.h).
//
// Each case below is a behaviour the box has shipped wrong, or a contract a shipped fix relies on. The ones that
// name a report are regressions : each was verified to FAIL with the defect put back (the mutation named in its
// comment) before it was kept.
#include "check.h"
#include "fake_game.h"
#include "fake_packets.h"
#include "model/party_state.h"
#include "model/ui_config.h"
#include <cstring>

using namespace aio;
using namespace fake;

namespace {
const unsigned ME = 0x00010001u, KAO = 0x00010002u, GAB = 0x00010003u, MONB = 0x00010004u;
const unsigned short ST_HASTE = 33, ST_REFRESH = 43, ST_PHALANX = 116, ST_PROTECT = 40, ST_MARCH = 214;
const unsigned SP_HASTE = 57, SP_REFRESH = 109, SP_PHALANX2 = 107, SP_PROTECTRA5 = 129, SP_HONOR_MARCH = 417;
const unsigned MSG_LANDED = 236;

// Default config for every case, set explicitly : a test must not depend on what the previous one left behind.
void config_defaults() {
    UiConfig& c = ui_config();
    c.tmMine = 1; c.tmBuffSrc = TMSRC_ALL; c.tmMax = 50; c.tmSortDur = 0; c.tmSortRec = 0;
    c.tmFocusWarn = 60; c.tmFocusHold = 60;
    c.tmBuffOffN = 0;
}
void focus_on(unsigned short status) { ui_config().tm_buff_set(UiConfig::TM_KEY_FOCUS | status, true); }

// An RDM with a player and a trust in the party.
void rdm_party() {
    world({ { ME, "Tetsouo", 5, false }, { KAO, "Kaories", 17, false }, { GAB, "Gab", 1, false }, { MONB, "Monberaux", 0, true } });
    config_defaults();
    settle();
}
}  // namespace

void test_timers() {
    if (selfcheck()) { printf("  the fake game does not reach the model -- the Timers cases would prove nothing, skipped\n"); return; }

    SECTION("timers : the packet builders speak the parser's layout");
    {
        rdm_party();
        deliver(pkt_self_timers({ { ST_HASTE, 180 }, { ST_REFRESH, 150 } }));
        int n = 0; const BuffTimer* bt = party().buff_timers(n);
        CHECK_EQ(n, 2);
        if (n == 2) { CHECK_EQ(bt[0].id, ST_HASTE); CHECK_EQ(bt[0].expiry, now_tick() + 180u * 60u); CHECK_EQ(bt[1].id, ST_REFRESH); }
        deliver(pkt_party_buffs({ { KAO, { ST_HASTE, 0x1F0 /* hi bits used */, ST_REFRESH } } }));
        const BuffSet* bs = party().buffs_for(KAO);
        CHECK(bs != 0);
        if (bs) { CHECK_EQ(bs->n, 3); CHECK_EQ(bs->ids[0], ST_HASTE); CHECK_EQ(bs->ids[1], 0x1F0); CHECK_EQ(bs->ids[2], ST_REFRESH); }
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));
        int no = 0; const PartyState::OtherBuff* ob = party().other_buffs(no);
        CHECK_EQ(no, 1);
        if (no == 1) { CHECK_EQ(ob[0].target, KAO); CHECK_EQ(ob[0].status, ST_HASTE); CHECK_EQ(ob[0].spell, SP_HASTE); CHECK_EQ(ob[0].aoe, 0); CHECK_STR(ob[0].name, "Kaories"); }
    }

    SECTION("timers : a Haste you cast on one ally is that ally's row");
    {
        rdm_party();
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));
        TimersRows r; step(r);
        const int i = find_row(r, ST_HASTE, "Kaories");
        CHECK(i >= 0);
        if (i >= 0) { CHECK_EQ(r.bufs[i].rem, 180); CHECK_EQ(r.bufs[i].src, 5); CHECK_STR(r.bufs[i].name, "Haste"); }
        if (i < 0) dump(r, "haste on Kaories");
    }

    SECTION("timers : a full party of focused buffs fits the monitor (FOCUS_MAX 64, not the 24 it once was)");
    {   // FOUND BY READING, 2026-09-13 : the monitor grew from 24 to 64 entries on 2026-09-11, but the per-entry verdict
        // array beside it (`static bool fmHas[24]`) did not. Past 24 watched buffs every frame wrote and read beyond it
        // -- into whatever static the linker placed next. An RDM keeping six buffs on five allies is 30 entries.
        // Out-of-bounds on a static rarely shows as a wrong row, so this case is judged by the address sanitizer the
        // suite is built with (tests.bat /fsanitize=address) : it stops the run at the first out-of-bounds byte.
        world({ { ME, "Tetsouo", 5, false }, { KAO, "Kaories", 17, false }, { GAB, "Gab", 1, false },
                { 0x00010005u, "Aeryn", 3, false }, { 0x00010006u, "Byrth", 4, false }, { 0x00010007u, "Cid", 6, false } });
        config_defaults();
        static const unsigned spells[6] = { SP_HASTE, SP_REFRESH, SP_PHALANX2, 47 /* Protect V */, 52 /* Shell V */, 108 /* Regen */ };
        static const unsigned short statuses[6] = { ST_HASTE, ST_REFRESH, ST_PHALANX, ST_PROTECT, 41, 42 };
        for (int s = 0; s < 6; ++s) focus_on(statuses[s]);
        const unsigned allies[5] = { KAO, GAB, 0x00010005u, 0x00010006u, 0x00010007u };
        settle();
        for (int a = 0; a < 5; ++a) for (int s = 0; s < 6; ++s) { deliver(pkt_cast(ME, spells[s], { { allies[a], MSG_LANDED } })); advance_ms(10); }
        deliver(pkt_party_buffs({ { KAO, { 33, 43, 116, 40, 41, 42 } }, { GAB, { 33, 43, 116, 40, 41, 42 } }, { 0x00010005u, { 33, 43, 116, 40, 41, 42 } },
                                  { 0x00010006u, { 33, 43, 116, 40, 41, 42 } }, { 0x00010007u, { 33, 43, 116, 40, 41, 42 } } }));
        TimersRows r;
        for (int f = 0; f < 3; ++f) { advance_ms(16); step(r); }
        int perAlly = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 5) ++perAlly;
        CHECK_EQ(perAlly, 30);
        int marked = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 5 && r.bufs[i].mark > 0) ++marked;
        // Every watched row carries the number //aio out takes. The same stale 24 lived in fm_free_tag : past it an
        // entry got tag 0, which on a row reads "not monitored" and which no //aio out <n> can name.
        CHECK_EQ(marked, 30);
        // Lose one on the LAST entry created -- the 30th, far past index 24 -- and the red OUT must say so.
        deliver(pkt_party_buffs({ { KAO, { 33, 43, 116, 40, 41, 42 } }, { GAB, { 33, 43, 116, 40, 41, 42 } }, { 0x00010005u, { 33, 43, 116, 40, 41, 42 } },
                                  { 0x00010006u, { 33, 43, 116, 40, 41, 42 } }, { 0x00010007u, { 33, 43, 116, 40, 41 } } }));
        advance_ms(16); step(r);
        int out = -1; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6 && r.bufs[i].icon == 42) out = i;
        CHECK(out >= 0);
        if (out >= 0) CHECK_STR(r.bufs[out].who, "Cid");
        if (out < 0) dump(r, "30 focused ally buffs, Cid lost Regen");
    }

    SECTION("timers : more ally buffs than the old table held (OB_MAX 128, not the 32 it once was)");
    {   // FOUND BY READING, 2026-09-13 -- the twin of the defect above, one layer down. The ally-buff table grew from 32 to
        // 128 rows (42212c2), but the model's per-frame prune kept `bool drop[32]; const char* why[32]` on the STACK and
        // walks them to otherBuffN_. Past 32 of your buffs on allies, every frame wrote past the end of both. Seven buffs
        // on five allies is 35. Judged by the address sanitizer (stack-buffer-overflow), and by the rows being all there.
        world({ { ME, "Tetsouo", 5, false }, { KAO, "Kaories", 17, false }, { GAB, "Gab", 1, false },
                { 0x00010005u, "Aeryn", 3, false }, { 0x00010006u, "Byrth", 4, false }, { 0x00010007u, "Cid", 6, false } });
        config_defaults();
        settle();
        static const unsigned spells[7] = { SP_HASTE, SP_REFRESH, SP_PHALANX2, 47 /* Protect V */, 52 /* Shell V */, 108 /* Regen */, 53 /* Blink */ };
        const unsigned allies[5] = { KAO, GAB, 0x00010005u, 0x00010006u, 0x00010007u };
        for (int a = 0; a < 5; ++a) for (int s = 0; s < 7; ++s) { deliver(pkt_cast(ME, spells[s], { { allies[a], MSG_LANDED } })); advance_ms(10); }
        int no = 0; party().other_buffs(no);
        CHECK_EQ(no, 35);
        TimersRows r;
        for (int f = 0; f < 5; ++f) { advance_ms(1000); step(r); }   // past the 3 s "fresh" window : the prune now judges every entry
        party().other_buffs(no);
        CHECK_EQ(no, 35);
        int perAlly = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 5) ++perAlly;
        CHECK_EQ(perAlly, 35);
    }

    SECTION("timers : an ally we hold no buff list for keeps the row ; one whose list lacks the buff loses it");
    {   // Rule 10, "empty is not unavailable", in its costliest form : buffs_for() == null used to be read as "the buff
        // is gone", which emptied the row of every trust, and of real players until a 0x076 arrived (verified in game :
        // five trusts and a player all buffset=NONE while the model held the right rows).
        // Mutation : per-ally emit `if (known && !has && !graceOB)` -> `if (!has && !graceOB)`.
        rdm_party();
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED }, }));
        deliver(pkt_cast(ME, SP_HASTE, { { MONB, MSG_LANDED } }));
        TimersRows r;
        advance_ms(4000); step(r);                    // past the prune's 3 s "fresh" window, still no 0x076 at all
        CHECK(find_row(r, ST_HASTE, "Kaories") >= 0);
        CHECK(find_row(r, ST_HASTE, "Monberaux") >= 0);
        deliver(pkt_party_buffs({ { KAO, { ST_REFRESH } } }));   // now we KNOW Kaories' list, and Haste is not in it
        advance_ms(16); step(r);
        CHECK_EQ(find_row(r, ST_HASTE, "Kaories"), -1);
        CHECK(find_row(r, ST_HASTE, "Monberaux") >= 0);
    }

    SECTION("timers : a cast the server says did nothing leaves no row");
    {   // Message 75 ("no effect") on a target : a weaker re-cast, or a resist. Recorded anyway, it drew a second live
        // timer for one buff. Mutation : ally loop `if (is_no_land_msg(tgtMsg[i])) ... continue;` removed.
        rdm_party();
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, 75 } }));
        TimersRows r; advance_ms(16); step(r);
        CHECK_EQ(find_row(r, ST_HASTE, "Kaories"), -1);
        int no = 0; party().other_buffs(no);
        CHECK_EQ(no, 0);
    }

    SECTION("timers : //aio out takes a row off, a newer cast of it puts it back, //aio in undoes");
    {   // The mute lives as long as the CAST it silenced. The usual correction after a mistaken //aio out is to cast the
        // buff again -- and Haste overwrites itself, so without the newer-cast rule the row stayed hidden for as long as
        // it was kept up. Mutation : the FOCUS_LIFT loop (focus_mute_verdict == FOCUS_LIFT) removed.
        rdm_party();
        focus_on(ST_HASTE);
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));
        deliver(pkt_party_buffs({ { KAO, { ST_HASTE } } }));
        TimersRows r; advance_ms(16); step(r);
        advance_ms(16); step(r);   // the monitor seeds its entry AFTER this frame's rows : the number shows from the next one
        int i = find_row(r, ST_HASTE, "Kaories");
        CHECK(i >= 0);
        const int number = i >= 0 ? r.bufs[i].mark : 0;
        CHECK(number > 0);
        char num[8]; _snprintf(num, sizeof(num), "%d", number); num[7] = 0;
        CHECK_EQ(timers_focus_forget(num, 0), 1);
        advance_ms(16); step(r);
        CHECK_EQ(find_row(r, ST_HASTE, "Kaories"), -1);
        advance_ms(20000);
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));   // cast again, on purpose
        advance_ms(16); step(r);
        advance_ms(16); step(r);   // the lift is decided after this frame's rows, like the seeding : visible from the next
        i = find_row(r, ST_HASTE, "Kaories");
        CHECK(i >= 0);
        if (i >= 0) CHECK_EQ(r.bufs[i].mark, number);             // same entry, same number
        CHECK_EQ(timers_focus_forget("kao", "haste"), 1);          // by name prefix, either order
        advance_ms(16); step(r);
        CHECK_EQ(find_row(r, ST_HASTE, "Kaories"), -1);
        CHECK_EQ(timers_focus_restore(0, 0), 1);
        advance_ms(16); step(r);
        CHECK(find_row(r, ST_HASTE, "Kaories") >= 0);
        CHECK_EQ(timers_focus_restore(0, 0), 0);                   // nothing left to put back : an honest zero
    }

    SECTION("timers : Phalanx cast one by one on three people is three people, up and lost");
    {   // Reported from play 2026-09-12 : three Phalanx placed one by one, all lost, came out as ONE nameless
        // "Phalanx II (AoE 3)" alert and the other two were dropped. Only a real AoE may group.
        // Mutation : alert grouping `ae[a].aoe = fm[alertQ[a]].aoe` -> `ae[a].aoe = 1`.
        world({ { ME, "Tetsouo", 5, false }, { KAO, "Kaories", 17, false }, { GAB, "Gab", 1, false }, { 0x00010005u, "Aeryn", 3, false } });
        config_defaults();
        settle();
        focus_on(ST_PHALANX);
        deliver(pkt_cast(ME, SP_PHALANX2, { { KAO, MSG_LANDED } })); advance_ms(2500);
        deliver(pkt_cast(ME, SP_PHALANX2, { { GAB, MSG_LANDED } })); advance_ms(2500);
        deliver(pkt_cast(ME, SP_PHALANX2, { { 0x00010005u, MSG_LANDED } }));
        deliver(pkt_party_buffs({ { KAO, { ST_PHALANX } }, { GAB, { ST_PHALANX } }, { 0x00010005u, { ST_PHALANX } } }));
        TimersRows r; advance_ms(16); step(r);
        CHECK_EQ(count_rows(r, ST_PHALANX), 3);
        CHECK(find_row(r, ST_PHALANX, "Kaories") >= 0);
        CHECK(find_row(r, ST_PHALANX, "Gab") >= 0);
        CHECK(find_row(r, ST_PHALANX, "Aeryn") >= 0);
        advance_ms(5000);
        deliver(pkt_party_buffs({ { KAO, { ST_REFRESH } }, { GAB, { ST_REFRESH } }, { 0x00010005u, { ST_REFRESH } } }));   // dispelled, all three
        for (int f = 0; f < 3; ++f) { advance_ms(16); step(r); }
        int alerts = 0, named = 0, hints = 0;
        for (int k = 0; k < r.nb; ++k) if (r.bufs[k].src == 6 && r.bufs[k].icon == ST_PHALANX) {
            ++alerts; if (r.bufs[k].who) ++named;
            if (r.bufs[k].post && strcmp(r.bufs[k].post, "  //aio out") == 0) ++hints;
        }
        CHECK_EQ(alerts, 3);
        CHECK_EQ(named, 3);
        // The correction hint rides the FIRST alert only : repeated down a column it is noise.
        // Mutation : the hint loop's `break` removed.
        CHECK_EQ(hints, 1);
        if (alerts != 3) dump(r, "three Phalanx lost");
    }

    SECTION("timers : Protect V on the party (Accession) then on one ally -- two casts, two timers");
    {   // Shipped defect : keyed only by (spell, fresh), the single-target re-cast merged into the AoE group of the SAME
        // spell, which drew ONE "(AoE N)" row on the SELF timer and hid the ally's newer, longer duration. The same
        // spell on both casts is the whole point -- two different spells never share a group, and an earlier version of
        // this case (Protectra V then Protect V) passed with the defect put back.
        // Mutations : pass-1 group key without `grp[k].aoe == ob[i].aoe` ; per-ally selector without `ob[i].aoe == grp[k].aoe`.
        rdm_party();
        deliver(pkt_cast(ME, 47 /* Protect V under Accession */, { { ME, MSG_LANDED }, { KAO, MSG_LANDED }, { GAB, MSG_LANDED } }));
        deliver(pkt_self_timers({ { ST_PROTECT, 1800 } }));
        self_buffs({ ST_PROTECT });
        deliver(pkt_party_buffs({ { KAO, { ST_PROTECT } }, { GAB, { ST_PROTECT } } }));
        TimersRows r;
        for (int f = 0; f < 4; ++f) { advance_ms(1000); step(r); }
        CHECK_EQ(count_rows(r, ST_PROTECT), 1);                     // the AoE : one row, you included
        const int g = find_row(r, ST_PROTECT, 0);
        CHECK(g >= 0);
        if (g >= 0) CHECK_STR(r.bufs[g].post, " (AoE 3)");
        advance_ms(300000);                                          // five minutes later, a fresh Protect V on Kaories alone
        deliver(pkt_cast(ME, 47, { { KAO, MSG_LANDED } }));
        for (int f = 0; f < 4; ++f) { advance_ms(1000); step(r); }
        const int k = find_row(r, ST_PROTECT, "Kaories");
        CHECK(k >= 0);
        if (k >= 0) CHECK(r.bufs[k].rem > 1700);                    // her own, newer timer -- not the self one (~1496 s)
        CHECK_EQ(find_row(r, ST_PROTECT, "Gab"), -1);               // Gab is still in the AoE group, not listed by name
        const int g2 = find_row(r, ST_PROTECT, 0);                  // ...and that group is still drawn AS a group : merged
        CHECK(g2 >= 0);                                              // under the single-target key, it silently lost Gab and
        if (g2 >= 0) CHECK(r.bufs[g2].post && strncmp(r.bufs[g2].post, " (AoE", 5) == 0);   // left your own row bare
        if (k < 0 || find_row(r, ST_PROTECT, "Gab") >= 0) dump(r, "Protect V AoE then Protect V on Kaories");
    }

    SECTION("timers : a zone forgets the songs on allies, and only the songs");
    {   // Reported 2026-09-12 : zoning threw away the Haste, Refresh and Phalanx put on the party -- thirty minutes of
        // buff whose only record was that row. Songs are re-sung in seconds and are dropped on purpose (asked for
        // 2026-09-11). Two layers carry the rule, and each has its mutation :
        //   model   : 0x00B handler `other_buffs_clear_songs()` -> `other_buffs_clear()`
        //   monitor : the zone pass `if (allySong) continue;` -> `if (!fm[q].self) continue;`
        rdm_party();
        focus_on(ST_HASTE);
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));
        deliver(pkt_cast(ME, SP_HASTE, { { GAB, MSG_LANDED } }));
        deliver(pkt_cast(ME, SP_HONOR_MARCH, { { ME, MSG_LANDED }, { KAO, MSG_LANDED }, { GAB, MSG_LANDED } }));
        deliver(pkt_self_timers({ { ST_MARCH, 120 } }));
        self_buffs({ ST_MARCH });
        deliver(pkt_party_buffs({ { KAO, { ST_HASTE, ST_MARCH } }, { GAB, { ST_HASTE, ST_MARCH } } }));
        TimersRows r;
        for (int f = 0; f < 3; ++f) { advance_ms(1000); step(r); }
        CHECK(find_row(r, ST_HASTE, "Kaories") >= 0);
        CHECK_EQ(timers_focus_forget("gab", "haste"), 1);               // the Haste on Gab was a mistake : taken off
        zone_to(231);
        self_buffs({});
        settle();
        deliver(pkt_party_buffs({ { KAO, { ST_HASTE } }, { GAB, { ST_HASTE } } }));
        for (int f = 0; f < 2; ++f) { advance_ms(1000); step(r); }
        CHECK(find_row(r, ST_HASTE, "Kaories") >= 0);                   // the Haste survived the zone
        CHECK_EQ(find_row(r, ST_HASTE, "Gab"), -1);                      // ...and so did taking Gab's off : the cast it silenced is still up
        CHECK_EQ(count_rows(r, ST_MARCH), 0);                            // the song rows did not
        int no = 0; const PartyState::OtherBuff* ob = party().other_buffs(no);
        int songs = 0; for (int i = 0; i < no; ++i) if (ob[i].status == ST_MARCH) ++songs;
        CHECK_EQ(songs, 0);
        // ...and the monitor still watches that Haste : losing it after the zone raises the red OUT, with her name.
        deliver(pkt_party_buffs({ { KAO, { ST_REFRESH } }, { GAB, { ST_HASTE } } }));
        for (int f = 0; f < 3; ++f) { advance_ms(1000); step(r); }
        int out = -1; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6 && r.bufs[i].icon == ST_HASTE) out = i;
        CHECK(out >= 0);
        if (out >= 0) CHECK_STR(r.bufs[out].who, "Kaories");
        if (out < 0) dump(r, "Haste lost after a zone");
    }

    SECTION("timers : turning 'my buffs on allies' off silences their alerts too");
    {   // An already-watched ally entry kept drawing a red "Kaories - Haste OUT" for a category just switched off.
        // Mutation : focus emit `if (!fm[q].self && !C.tmMine) { ... continue; }` removed.
        rdm_party();
        focus_on(ST_HASTE);
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));
        deliver(pkt_party_buffs({ { KAO, { ST_HASTE } } }));
        TimersRows r;
        for (int f = 0; f < 2; ++f) { advance_ms(1000); step(r); }
        advance_ms(4000);
        deliver(pkt_party_buffs({ { KAO, { ST_REFRESH } } }));
        for (int f = 0; f < 2; ++f) { advance_ms(16); step(r); }
        int alerts = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6) ++alerts;
        CHECK_EQ(alerts, 1);
        ui_config().tmMine = 0;
        advance_ms(16); step(r);
        alerts = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6) ++alerts;
        CHECK_EQ(alerts, 0);
        ui_config().tmMine = 1;
    }

    SECTION("timers : your own focused buff lost -- an EMPTY list is an answer, an unreadable one is not");
    {   // Measured : nbuff=0 with has=1 on 7783 consecutive samples -- "no buffs" was read as "no data", so the lost-buff
        // alert could NEVER fire, because an empty list is exactly the state after your last buff wears.
        // Mutations : focus listReady (self) `f.game->buffsOk` -> `f.game->nbuff > 0` ; focus emit (self)
        // `(!f.game || !f.game->buffsOk || fmHas[q])` -> `(!f.game || fmHas[q])`.
        rdm_party();
        focus_on(ST_HASTE);
        deliver(pkt_cast(ME, SP_HASTE, { { ME, MSG_LANDED } }));
        deliver(pkt_self_timers({ { ST_HASTE, 180 } }));
        self_buffs({ ST_HASTE });
        TimersRows r;
        for (int f = 0; f < 2; ++f) { advance_ms(16); step(r); }
        CHECK(find_row(r, ST_HASTE, 0) >= 0);
        // The read FAILS (a zone, a not-ready player struct) : unknown, so no alert.
        deliver(pkt_self_timers({}));
        self_buffs_unreadable();
        for (int f = 0; f < 2; ++f) { advance_ms(16); step(r); }
        int alerts = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6 && r.bufs[i].icon == ST_HASTE) ++alerts;
        CHECK_EQ(alerts, 0);
        // The read SUCCEEDS and the list is empty : the Haste is gone, and the box must say so.
        self_buffs({});
        for (int f = 0; f < 2; ++f) { advance_ms(16); step(r); }
        alerts = 0; int who = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6 && r.bufs[i].icon == ST_HASTE) { ++alerts; if (r.bufs[i].who) ++who; }
        CHECK_EQ(alerts, 1);
        CHECK_EQ(who, 0);                                                 // yours : no person named
        if (alerts != 1) dump(r, "self Haste lost, empty list");
    }

    SECTION("timers : a song on the party is one row ; the member a re-sing missed is named on the old timer");
    {   // The fresh / laggard split. A song copy on an ally is FRESH only while it tracks your CURRENT cast ; one a
        // re-sing missed keeps the older, shorter timer and must be listed by name, never folded into the new group.
        // Mutation : obFresh `return d <= 600;` -> `return true;`.
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 17, false }, { GAB, "Gab", 1, false } });
        config_defaults();
        settle();
        deliver(pkt_cast(ME, SP_HONOR_MARCH, { { ME, MSG_LANDED }, { KAO, MSG_LANDED }, { GAB, MSG_LANDED } }));
        deliver(pkt_self_timers({ { ST_MARCH, 120 } }));
        self_buffs({ ST_MARCH });
        deliver(pkt_party_buffs({ { KAO, { ST_MARCH } }, { GAB, { ST_MARCH } } }));
        TimersRows r;
        for (int f = 0; f < 4; ++f) { advance_ms(1000); step(r); }
        CHECK_EQ(count_rows(r, ST_MARCH), 1);
        int g = find_row(r, ST_MARCH, 0);
        CHECK(g >= 0);
        if (g >= 0) { CHECK_STR(r.bufs[g].name, "Honor March"); CHECK_STR(r.bufs[g].post, " (AoE 3)"); }
        if (count_rows(r, ST_MARCH) != 1) dump(r, "Honor March on the party");
        // 90 s later : re-sung, but Gab was out of range. NOT 40 s : below 60 s the model's zone re-align
        // (prune_other_buffs_worn, `selfE - expTick < 3600`) cannot tell a quick re-sing from a loading-screen bump
        // and folds the missed member back into the fresh group. A known limit, reported 2026-09-13, not asserted here.
        advance_ms(90000);
        deliver(pkt_cast(ME, SP_HONOR_MARCH, { { ME, MSG_LANDED }, { KAO, MSG_LANDED } }));
        deliver(pkt_self_timers({ { ST_MARCH, 120 } }));
        for (int f = 0; f < 4; ++f) { advance_ms(1000); step(r); }
        g = find_row(r, ST_MARCH, 0);
        const int lag = find_row(r, ST_MARCH, "Gab");
        CHECK(g >= 0);
        CHECK(lag >= 0);
        if (g >= 0) CHECK_STR(r.bufs[g].post, " (AoE 2)");
        if (g >= 0 && lag >= 0) CHECK(r.bufs[lag].rem < r.bufs[g].rem - 60);   // Gab's copy is the OLD cast, ~90 s shorter
        CHECK_EQ(find_row(r, ST_MARCH, "Kaories"), -1);                  // Kaories is in the fresh group, not named
        if (g < 0 || lag < 0) dump(r, "re-sing missed Gab");
    }
}
