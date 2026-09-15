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
const unsigned SP_HASTE = 57, SP_PHALANX = 106, SP_REFRESH = 109, SP_PHALANX2 = 107, SP_PROTECTRA5 = 129, SP_HONOR_MARCH = 417;
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

    SECTION("actions : the model's 0x028 decode returns what was written, target by target");
    {   // model_decode_action is what the in-game packet witness compares with Windower's own parser ; this pins it to
        // the bit layout first. Three targets, each with its own message and param.
        const Packet p = pkt_cast(ME, 57, { { KAO, 230, 33 }, { GAB, 75, 0 }, { MONB, 236, 1234 } });
        ActionDecode d;
        CHECK(model_decode_action(p.b, d));
        CHECK_EQ(d.actor, ME); CHECK_EQ(d.category, 4u); CHECK_EQ(d.param, 57u); CHECK_EQ(d.n, 3u);
        CHECK_EQ(d.ids[0], KAO); CHECK_EQ(d.msgs[0], 230u); CHECK_EQ(d.params[0], 33u);
        CHECK_EQ(d.ids[1], GAB); CHECK_EQ(d.msgs[1], 75u);
        CHECK_EQ(d.ids[2], MONB); CHECK_EQ(d.msgs[2], 236u); CHECK_EQ(d.params[2], 1234u);
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
        CHECK_EQ(out(num, 0), 1);
        advance_ms(16); step(r);
        CHECK_EQ(find_row(r, ST_HASTE, "Kaories"), -1);
        advance_ms(20000);
        deliver(pkt_cast(ME, SP_HASTE, { { KAO, MSG_LANDED } }));   // cast again, on purpose
        advance_ms(16); step(r);
        advance_ms(16); step(r);   // the lift is decided after this frame's rows, like the seeding : visible from the next
        i = find_row(r, ST_HASTE, "Kaories");
        CHECK(i >= 0);
        if (i >= 0) CHECK_EQ(r.bufs[i].mark, number);             // same entry, same number
        CHECK_EQ(out("kao", "haste"), 1);          // by name prefix, either order
        advance_ms(16); step(r);
        CHECK_EQ(find_row(r, ST_HASTE, "Kaories"), -1);
        CHECK_EQ(in(0, 0), 1);
        advance_ms(16); step(r);
        CHECK(find_row(r, ST_HASTE, "Kaories") >= 0);
        CHECK_EQ(in(0, 0), 0);                   // nothing left to put back : an honest zero
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

    SECTION("timers : Phalanx II then Accession Phalanx -- one buff at another tier, no red OUT");
    {   // Reported from play 2026-09-15 : "il fait Phalanx II sur tout le monde puis en RDM/SCH il fait Accession
        // Phalanx, les Phalanx II sont indiques en OUT alors que tout le monde a bien Phalanx". Both tiers are
        // status 116 and the game holds ONE of them, so the second cast REPLACED the first -- the model says so
        // (it drops that target's other rows on the same status) but the monitor, keyed by spell for the songs'
        // sake, kept the old entry, counted the new one as a newer sibling over a single live copy and called the
        // replacement a loss. Permanent red OUT for a buff everybody had.
        // Mutation, MEASURED : put the shipped shape back -- focusHas counting siblings for every buff instead of
        // `focus_entry_up(several, ...)`, AND the prune's `if (e.tierReplaced()) return FP_DROP_TIER_REPLACED;`
        // removed. The box then draws exactly the report, side by side:
        //     0. icon=116 src=6  "Phalanx II (AoE 2)"  <- red OUT
        //     1. icon=116 src=4  "Phalanx (AoE 3)" 176s <- everybody has it
        // Removing only the prune drop is a different, later defect the second half of this case pins: the old
        // tier says nothing while the buff is up, then doubles EVERY alert the day it really goes (four rows, two
        // of them for a spell last cast half an hour ago). The `several` gate itself is held by t_focusrules
        // (focus_entry_up) -- with the drop in place its effect here is one frame wide.
        rdm_party();
        focus_on(ST_PHALANX);
        deliver(pkt_cast(ME, SP_PHALANX2, { { ME, MSG_LANDED }, { KAO, MSG_LANDED }, { GAB, MSG_LANDED } }));   // Phalanx II on everyone, yourself included
        deliver(pkt_self_timers({ { ST_PHALANX, 240 } }));
        self_buffs({ ST_PHALANX });
        deliver(pkt_party_buffs({ { KAO, { ST_PHALANX } }, { GAB, { ST_PHALANX } } }));
        TimersRows r;
        for (int f = 0; f < 3; ++f) { advance_ms(1000); step(r); }
        CHECK_EQ(count_rows(r, ST_PHALANX), 1);                      // the AoE : one row, you included
        advance_ms(30000);
        deliver(pkt_cast(ME, SP_PHALANX, { { ME, MSG_LANDED }, { KAO, MSG_LANDED }, { GAB, MSG_LANDED } }));   // Accession Phalanx : the OTHER tier, on all three
        deliver(pkt_self_timers({ { ST_PHALANX, 180 } }));                                                     // ...and it is SHORTER than what is left of the old one
        self_buffs({ ST_PHALANX });
        deliver(pkt_party_buffs({ { KAO, { ST_PHALANX } }, { GAB, { ST_PHALANX } } }));                        // everybody still has Phalanx
        // COUNTED ON EVERY FRAME, never once at the end : the self entry ranks by EXPIRY, so the shorter new tier
        // ranks BELOW the corpse of the longer old one and the copy arithmetic calls the LIVE one the missing copy.
        // That is a one-frame red flash the frame the replacement lands -- invisible to a check that looks later,
        // and the reason the rule reads presence for a single-instance buff instead of counting siblings.
        int outs = 0;
        for (int f = 0; f < 4; ++f) { advance_ms(1000); step(r);
            for (int k = 0; k < r.nb; ++k) if (r.bufs[k].src == 6 && r.bufs[k].icon == ST_PHALANX) ++outs; }
        CHECK_EQ(outs, 0);                                            // THE REPORT : not one red OUT, not on one frame
        const int g = find_row(r, ST_PHALANX, 0);                     // and the live buff is drawn once, as the AoE it was
        CHECK(g >= 0);
        if (g >= 0) CHECK_STR(r.bufs[g].post, " (AoE 3)");
        if (g >= 0) CHECK_STR(r.bufs[g].name, "Phalanx");              // named for the tier that is UP, not the one it replaced
        CHECK_EQ(count_rows(r, ST_PHALANX), 1);                       // the replaced tier left no second row either
        if (outs) dump(r, "Phalanx II replaced by Accession Phalanx");
        // ...AND THE MONITOR STILL WATCHES WHAT IT KEPT. Forgetting the replaced tier must not forget the buff:
        // when the new one really goes, the red OUT it exists for fires -- once, not once per tier ever cast.
        deliver(pkt_party_buffs({ { KAO, {} }, { GAB, {} } }));
        self_buffs({});
        deliver(pkt_self_timers({}));
        for (int f = 0; f < 3; ++f) { advance_ms(1000); step(r); }
        int outs2 = 0, oldTier = 0; for (int k = 0; k < r.nb; ++k) if (r.bufs[k].src == 6 && r.bufs[k].icon == ST_PHALANX) {
            ++outs2; if (r.bufs[k].name && strcmp(r.bufs[k].name, "Phalanx II") == 0) ++oldTier; }
        CHECK_EQ(outs2, 2);             // yours, and one grouped line for the two allies the AoE reached
        CHECK_EQ(oldTier, 0);           // ...and not one of them speaks for the tier that was replaced half an hour ago
        if (outs2 != 2 || oldTier) dump(r, "Phalanx really lost after the tier change");
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
        CHECK_EQ(out("gab", "haste"), 1);               // the Haste on Gab was a mistake : taken off
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

    SECTION("timers : PLD/RUN changing runes -- only the two runes placed last may go OUT");
    {   // Reported 2026-09-14 : every rune changed raised a permanent red OUT. The game holds 2 runes on a RUN sub and
        // drops the OLDEST for a new one. Mutation : the prune/alert without `runeReplaced()` (focus_rules.h section 10).
        const unsigned short IGNIS = 523, FLABRA = 525, TENEBRAE = 530;
        world({ { ME, "Tetsouo", 7, false }, { KAO, "Kaories", 17, false } });
        config_defaults();
        self_jobs(7, 99, 22, 49);
        settle();
        focus_on(IGNIS); focus_on(FLABRA); focus_on(TENEBRAE);
        auto outs = [](const TimersRows& r, unsigned short st) { int n = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6 && r.bufs[i].icon == st) ++n; return n; };
        TimersRows r;
        auto runes = [&](std::initializer_list<unsigned short> on) {
            Packet p = pkt_self_timers({});                               // the 0x063 timers and the memory list, together
            if (on.size() == 1) p = pkt_self_timers({ { on.begin()[0], 300 } });
            if (on.size() == 2) p = pkt_self_timers({ { on.begin()[0], 300 }, { on.begin()[1], 300 } });
            deliver(p); self_buffs(on);
            for (int f = 0; f < 3; ++f) { advance_ms(16); step(r); } };
        runes({ IGNIS, FLABRA });
        runes({ FLABRA, TENEBRAE });                                      // Ignis pushed out by Tenebrae : a swap, not a loss
        CHECK_EQ(outs(r, IGNIS), 0);
        if (outs(r, IGNIS)) dump(r, "PLD/RUN Ignis pushed out by Tenebrae");
        runes({ TENEBRAE });                                              // Flabra ran out with a slot free : that is a loss
        CHECK_EQ(outs(r, FLABRA), 1);
        CHECK_EQ(outs(r, IGNIS), 0);                                      // the Ignis swapped out earlier stays forgotten
        runes({ TENEBRAE, IGNIS });                                       // a NEW rune fills the slot Flabra wanted : its OUT goes
        CHECK_EQ(outs(r, FLABRA), 0);
        runes({});                                                        // everything consumed (Lunge) : the last two placed go OUT
        CHECK_EQ(outs(r, TENEBRAE), 1);
        CHECK_EQ(outs(r, IGNIS), 1);
        CHECK_EQ(outs(r, FLABRA), 0);
    }

    SECTION("timers : PLD/RUN, two Ignis run out -- two Ignis OUT, and a new rune quiets the older one");
    {   // Reported 2026-09-14 : "les deux ignis il reste que un avec out au lieu des deux". Each rune placed is watched on
        // its own (matched by its expiry). Mutation : the seeding by status (`fm[q].spell == selfSp`) for runes too.
        const unsigned short IGNIS = 523, FLABRA = 525;
        world({ { ME, "Tetsouo", 7, false }, { KAO, "Kaories", 17, false } });
        config_defaults();
        self_jobs(7, 99, 22, 49);
        settle();
        focus_on(IGNIS); focus_on(FLABRA);
        auto outs = [](const TimersRows& r, unsigned short st) { int n = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].src == 6 && r.bufs[i].icon == st) ++n; return n; };
        TimersRows r;
        auto frames = [&]() { for (int f = 0; f < 3; ++f) { advance_ms(16); step(r); } };
        deliver(pkt_self_timers({ { IGNIS, 300 } })); self_buffs({ IGNIS }); frames();
        advance_ms(6000);                                                  // the second Ignis, six seconds later
        deliver(pkt_self_timers({ { IGNIS, 294 }, { IGNIS, 300 } })); self_buffs({ IGNIS, IGNIS }); frames();
        CHECK_EQ(outs(r, IGNIS), 0);
        deliver(pkt_self_timers({ { IGNIS, 300 } })); self_buffs({ IGNIS }); frames();   // the first runs out, a slot is free
        CHECK_EQ(outs(r, IGNIS), 1);
        deliver(pkt_self_timers({})); self_buffs({}); frames();            // the second too
        CHECK_EQ(outs(r, IGNIS), 2);
        if (outs(r, IGNIS) != 2) dump(r, "PLD/RUN two Ignis run out");
        deliver(pkt_self_timers({ { FLABRA, 300 } })); self_buffs({ FLABRA }); frames();   // a new rune takes one slot
        CHECK_EQ(outs(r, IGNIS), 1);                                       // the last two placed : the newer Ignis and Flabra
        deliver(pkt_self_timers({ { FLABRA, 294 }, { FLABRA, 300 } })); self_buffs({ FLABRA, FLABRA }); frames();
        CHECK_EQ(outs(r, IGNIS), 0);
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

    SECTION("timers : a player's Protect that REPLACES yours is theirs, not yours");
    {   // MEASURED 2026-09-13 in game : Kaories (RDM) held her own Protect IV (5790 s left) ; Tetsouo cast Protect IV on
        // her ; the server replaced it (1957 s left). Her model still credited HER : the cast ring paired the live timer
        // with Tetsouo's cast (closest predicted expiry), but the "a later cast re-applied it" check then preferred the
        // status latch -- and the latch had REFUSED Tetsouo's landing, because a guard written for SONGS ("your live song
        // is not stolen by a trust singing another march on the same status") also covered a buff that has ONE instance.
        // Consequence : under "Mine only" another player's Protect showed as yours ; its loss would alert as yours.
        // Mutation : the song-only scope of the guard removed.
        world({ { ME, "Kaories", 5, false }, { GAB, "Tetsouo", 7, false } });
        config_defaults();
        settle();
        const unsigned MSG_GAINS = 230;   // "<target> gains the effect of <status>"
        deliver(pkt_cast(ME, 46 /* Protect IV */, { { ME, MSG_GAINS, ST_PROTECT } }));
        deliver(pkt_self_timers({ { ST_PROTECT, 5795 } }));
        self_buffs({ ST_PROTECT });
        TimersRows r; advance_ms(1000); step(r);
        { int n = 0; const BuffTimer* bt = party().buff_timers(n);
          CHECK_EQ(n, 1);
          if (n == 1) CHECK_EQ(party().buff_caster_for(ST_PROTECT, bt[0].expiry, 0), ME); }
        advance_ms(60000);
        deliver(pkt_cast(GAB, 46, { { ME, MSG_GAINS, ST_PROTECT } }));
        deliver(pkt_self_timers({ { ST_PROTECT, 1962 } }));
        advance_ms(1000); step(r);
        { int n = 0; const BuffTimer* bt = party().buff_timers(n);
          CHECK_EQ(n, 1);
          if (n == 1) CHECK_EQ(party().buff_caster_for(ST_PROTECT, bt[0].expiry, 0), GAB); }
        ui_config().tmBuffSrc = TMSRC_MINE;
        advance_ms(16); step(r);
        CHECK_EQ(count_rows(r, ST_PROTECT), 0);                          // "Mine only" : Tetsouo's Protect is not yours
        ui_config().tmBuffSrc = TMSRC_ALL;
    }

    SECTION("timers : a trust singing ANOTHER march does not take yours -- the songs keep their guard");
    {   // The case the guard was written for (MEASURED 2026-07-20) : marches share status 214, so a trust's Victory March
        // landing on you while your Honor March is live must not re-credit YOUR timer to the trust.
        world({ { ME, "Tetsouo", 10, false }, { MONB, "Ulmia", 0, true } });
        config_defaults();
        settle();
        const unsigned MSG_GAINS = 230;
        deliver(pkt_cast(ME, SP_HONOR_MARCH, { { ME, MSG_GAINS, ST_MARCH } }));
        deliver(pkt_self_timers({ { ST_MARCH, 300 } }));
        self_buffs({ ST_MARCH });
        TimersRows r; advance_ms(1000); step(r);
        advance_ms(20000);
        deliver(pkt_cast(MONB, 420 /* Victory March */, { { ME, MSG_GAINS, ST_MARCH } }));
        deliver(pkt_self_timers({ { ST_MARCH, 279 }, { ST_MARCH, 120 } }));
        self_buffs({ ST_MARCH, ST_MARCH });
        advance_ms(1000); step(r);
        int n = 0; const BuffTimer* bt = party().buff_timers(n);
        CHECK_EQ(n, 2);
        if (n == 2) CHECK_EQ(party().buff_caster_for(ST_MARCH, bt[0].expiry, 0), ME);   // the longer timer : your Honor March
    }

    SECTION("timers : an Indi- entrusted 20 s after Entrust is still an entrusted Indi-");
    {   // MEASURED 2026-09-13 in game : Entrust grants status 584 for 60 s (58 s left 2.5 s after the JA), and the game
        // entrusts the next Indi- cast inside that minute. The model armed Entrust for 15 s : an Indi- cast 19 s later
        // landed on Tetsouo (Colure Active 356 s) while the GEO's model drew no row for him at all.
        // Mutation : the Entrust window back to 15000 ms.
        world({ { ME, "Kaories", 21, false }, { GAB, "Tetsouo", 10, false } });
        config_defaults();
        settle();
        deliver(pkt_action(ME, 6, 386 /* Entrust */, { { ME, 100 } }));
        advance_ms(20000);
        deliver(pkt_cast(ME, 770 /* Indi-Refresh */, { { GAB, 230, 541 } }));
        TimersRows r; advance_ms(1000); step(r);
        int no = 0; const PartyState::OtherBuff* ob = party().other_buffs(no);
        int onGab = 0; for (int i = 0; i < no; ++i) if (ob[i].target == GAB && ob[i].spell == 770) ++onGab;
        CHECK_EQ(onGab, 1);
        CHECK(find_row(r, 541, "Tetsouo") >= 0);
        // ...and a normal Indi- (no Entrust in the last minute) is an aura, never an ally row.
        advance_ms(70000);
        deliver(pkt_cast(ME, 768 /* Indi-Regen */, { { GAB, 230, 539 } }));
        party().other_buffs(no);
        int regenOnGab = 0; ob = party().other_buffs(no); for (int i = 0; i < no; ++i) if (ob[i].target == GAB && ob[i].spell == 768) ++regenOnGab;
        CHECK_EQ(regenOnGab, 0);
    }

    SECTION("timers : an entrusted Indi- is estimated with its percent augments (the measured 355 s)");
    {   // The same measurement as t_durations, through on_action : the GEO's Entrust set, Entrust, then Indi-Refresh on the
        // ally. Mutation : the percent term dropped from the entrusted duration.
        world({ { ME, "Kaories", 21, false }, { GAB, "Tetsouo", 10, false } });
        config_defaults();
        settle();
        static const unsigned char GADA[24]      = { 0x02,0x03,0xE2,0x54,0x05,0x0A,0x23,0x80,0x85,0x68,0x2D,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        static const unsigned char LIFESTREAM[24] = { 0x02,0x03,0x2C,0x49,0xE2,0x9C,0x70,0x10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        unsigned short ids[16] = { 0 }; unsigned char ext[16][24] = { { 0 } };
        ids[0] = 21072; memcpy(ext[0], GADA, 24); ids[15] = 28637; memcpy(ext[15], LIFESTREAM, 24);
        ids[7] = 23619; ids[8] = 23708;
        equip(ids, ext);
        deliver(pkt_action(ME, 6, 386 /* Entrust */, { { ME, 100 } }));
        advance_ms(5000);
        deliver(pkt_cast(ME, 770 /* Indi-Refresh */, { { GAB, 230, 541 } }));
        int no = 0; const PartyState::OtherBuff* ob = party().other_buffs(no);
        int dur = -1; for (int i = 0; i < no; ++i) if (ob[i].target == GAB && ob[i].spell == 770) dur = (int)(ob[i].durMs / 1000u);
        // 180 base + 51 gear s ; this fake has no job points (read_jp_gift_rank = 0) : (180 + 51) x 1.31 = 302
        CHECK_EQ(dur, 302);
    }
}
