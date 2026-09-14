// t_packets.cpp -- the incoming packets no other case delivered : the Zone Tracker's five (0x02A zone messages, 0x055
// key items, 0x118 currencies, 0x034 the Rabao conflux menu, 0x075 battlefield bars), PointWatch's 0x02D gains, the
// 0x01B job info and the 0x068 pet status. Each one is fed, bit for bit (fake_packets.h), to the REAL handler through
// model_feed_packet, and judged on what a widget reads : zone_tracker(), limbus_coffers(), limbus_runs_left(),
// pointwatch(), encumbrance(), hate_rows().
//
// Every section carries the mutation it was proved against : the defect was put back in the handler, the suite run,
// THAT section seen failing, and the source restored byte for byte. A case that only walks the happy path is blind
// exactly where the defects of this file lived (a message base off by one, a label keyword missing, a false chip), so
// each handler also gets the packet it must NOT adopt.
#include "check.h"
#include "fake_game.h"
#include "fake_packets.h"
#include "model/party_state.h"
#include <cstring>

using namespace aio;
using namespace fake;

namespace {
const unsigned ME = 0x00040001u, KAO = 0x00040002u, GAB = 0x00040003u;
const ZoneTracker& zt() { return party().zone_tracker(); }
// A party of three standing in Southern San d'Oria (zone 230, no tracker mode), one frame in : the roster and the zone
// tracker have both seen the world once.
void fresh() { world({ { ME, "Tetsouo", 7, false }, { KAO, "Kaories", 5, false }, { GAB, "Gab", 15, false } }); frame(); }
// Walk to `zone` : the zone-out / loading / zone-in sequence, then the frame that runs zt_set_zone on the new zone.
void enter(unsigned zone) { zone_to(zone); frame(); }

// Zones, by id (model/zones.cpp).
const unsigned Z_KONSCHTAT = 15;     // "Abyssea - Konschtat" : Abyssea by name, base 7339
const unsigned Z_TEMENOS = 37, Z_APOLLYON = 38;
const unsigned Z_ALZADAAL = 72;      // the Nyzul staging point
const unsigned Z_DYN_SANDORIA = 185; // "Dynamis - San d'Oria" : a CITY Dynamis (granules 10/10/10/15/15 min)
const unsigned Z_RABAO = 247;
const unsigned Z_DIV_SANDORIA = 294; // "Dynamis - San d'Oria [D]" : Divergence, no granules
const unsigned Z_SHEOL = 298;
const int ABY = 7339;                // the Abyssea message base for every zone but 215/253 (measured 2026-09-09)

int lights_sum() { int s = 0; for (int i = 0; i < 7; ++i) s += zt().lights[i]; return s; }
}  // namespace

void test_packets() {
    if (selfcheck()) { printf("  the fake game does not reach the model -- the packet cases would prove nothing, skipped\n"); return; }

    SECTION("packets : the zone-tracker, pointwatch, job-info and pet builders speak their parsers' layout");
    {
        fresh();
        deliver(pkt_currency2(947485, 12000, 34000));
        CHECK_EQ(zt().segBank, 947485); CHECK_EQ(zt().limbusTemenos, 12000); CHECK_EQ(zt().limbusApollyon, 34000);
        deliver(pkt_job_info(0x8001));
        CHECK_EQ(party().encumbrance(), 0x8001u);
        deliver(pkt_exp_msg(718, 1200));
        CHECK_EQ(party().pointwatch().cpCur, 1200u);
        entity(0x700, 0x01000701u, "Carbuncle");
        deliver(pkt_pet_status(GAB, 0x700, 0x01001234u));
        CHECK(party().is_party_or_pet(0x01000701u));
        CHECK_EQ(party().hate_[0].mob, 0x01001234u);
        enter(Z_KONSCHTAT);
        deliver(pkt_zone_msg(ABY, 11, 22, 33, 44));
        CHECK_EQ(zt().lights[0], 11); CHECK_EQ(zt().lights[6], 22); CHECK_EQ(zt().lights[4], 33); CHECK_EQ(zt().lights[5], 44);
        enter(Z_DYN_SANDORIA);
        deliver(pkt_key_items(3, { 1545 }));
        CHECK_EQ(zt().ki[0], 1);
        enter(Z_RABAO);
        deliver(pkt_npc_menu(ME, 173, 3));
        CHECK_EQ(zt().sheolzone, 3);
        enter(Z_APOLLYON);
        deliver(pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 96, "SW_Floor_#3" } }));
        CHECK_STR(zt().limbusArea, "Apollyon"); CHECK_EQ(zt().limbusFloor, 3);
    }

    // ================================================ 0x02A ================================================
    SECTION("zone tracker 0x02A abyssea : the two /heal reports set all seven lights exactly, on base 7339");
    {   // Mutation : the base back to 7338 (aby_base_for_zone) -- the defect of 2026-09-09, where the first report landed
        // on offset 1 (azure/ruby/amber got pearl's values) and the second fell off the map.
        fresh(); enter(Z_KONSCHTAT);
        CHECK_EQ(zt().mode, 2);
        CHECK_EQ(zt().visitantMin, 5);                                   // the expulsion grace posted at entry
        deliver(pkt_zone_msg(ABY + 0, 150, 40, 120, 90));                // pearl, ebon, gold, silver
        deliver(pkt_zone_msg(ABY + 1, 200, 250, 30));                    // azure, ruby, amber
        CHECK_EQ(zt().lights[0], 150); CHECK_EQ(zt().lights[1], 200); CHECK_EQ(zt().lights[2], 250); CHECK_EQ(zt().lights[3], 30);
        CHECK_EQ(zt().lights[4], 120); CHECK_EQ(zt().lights[5], 90);  CHECK_EQ(zt().lights[6], 40);
    }

    SECTION("zone tracker 0x02A abyssea : a light gain adds up to its cap ; an unmapped id changes nothing");
    {   // Mutation : ruby's cap mistyped (`addL(2, 8, 255)` -> `addL(2, 8, 260)`) -- 258 on a 255 gauge.
        fresh(); enter(Z_KONSCHTAT);
        deliver(pkt_zone_msg(ABY + 0, 150, 40, 120, 90));
        deliver(pkt_zone_msg(ABY + 1, 200, 250, 30));
        deliver(pkt_zone_msg(ABY + 188, 0));                             // ruby +8, from 250 : capped at 255
        CHECK_EQ(zt().lights[2], 255);
        deliver(pkt_zone_msg(0x4000 | (ABY + 189), 0));                  // amber +8 ; the flag bits above the id are masked off
        CHECK_EQ(zt().lights[3], 38);
        deliver(pkt_zone_msg(ABY + 184, 2));                             // golden +5*(p1+1)
        CHECK_EQ(zt().lights[4], 135);
        deliver(pkt_zone_msg(ABY + 186, 3));                             // ebon +(p1+1)
        CHECK_EQ(zt().lights[6], 44);
        const int before = lights_sum();
        deliver(pkt_zone_msg(ABY + 50, 99, 99, 99, 99));                 // an id on no mapped offset
        CHECK_EQ(lights_sum(), before);
        CHECK_EQ(zt().visitantMin, 5);
    }

    SECTION("zone tracker 0x02A abyssea : visitant messages set and extend the minutes ; outside Abyssea nothing moves");
    {   // Mutation : the mode gate dropped (`if (zt_.mode != 2) return;`) -- any town's 0x02A would then write lights.
        // Mutation : the extension overwriting instead of adding (`visitantMin += p1` -> `visitantMin = p1`).
        fresh(); enter(Z_KONSCHTAT);
        deliver(pkt_zone_msg(ABY + 45, 120));                            // visitant status granted : 120 min
        CHECK_EQ(zt().visitantMin, 120);
        deliver(pkt_zone_msg(ABY + 12, 30));                             // extended by 30
        CHECK_EQ(zt().visitantMin, 150);
        fresh();                                                         // a town : mode 0
        deliver(pkt_zone_msg(ABY + 0, 150, 40, 120, 90));
        deliver(pkt_zone_msg(ABY + 45, 120));
        CHECK_EQ(lights_sum(), 0);
        CHECK_EQ(zt().visitantMin, 0);
    }

    SECTION("zone tracker 0x02A sheol : the payout counts from the first kill, and a duplicate does not double-count");
    {   // Mutation : the first payout's baseline taken as its total (`: (total - gain)` -> `: total`) -- the first kill of
        // every run lost, the counter one payout short all run.
        fresh(); enter(Z_RABAO); enter(Z_SHEOL);
        CHECK_EQ(zt().mode, 5);
        deliver(pkt_zone_msg(7249, 13, 947498));                         // the real capture : p1 = paid, p2 = banked total
        CHECK_EQ(zt().segments, 13);
        deliver(pkt_zone_msg(7249, 13, 947511));
        CHECK_EQ(zt().segments, 26);
        deliver(pkt_zone_msg(7249, 13, 947511));                         // a duplicated chunk
        CHECK_EQ(zt().segments, 26);
        deliver(pkt_zone_msg(7248, 23, 0));                              // the reused old id : paid nothing, banked nothing
        CHECK_EQ(zt().segments, 26);
    }

    SECTION("zone tracker 0x02A sheol : 298 not entered from Rabao is no run, and back in Rabao the total freezes");
    {   // Mutation : the "(last run)" freeze dropped (`if (zt_.segLastRun) return;`) -- a payout heard in Rabao moves it.
        // Mutation : the from-Rabao gate dropped in zt_set_zone (`&& oldZone == 247`) -- every Selbina HTMB becomes a run.
        fresh(); enter(Z_SHEOL);                                         // from San d'Oria : a Selbina HTMB, not Odyssey
        CHECK(zt().mode != 5);
        deliver(pkt_zone_msg(7249, 13, 1000));
        CHECK_EQ(zt().segments, 0);
        enter(Z_RABAO); enter(Z_SHEOL);
        deliver(pkt_zone_msg(7249, 13, 1000));
        CHECK_EQ(zt().segments, 13);
        enter(Z_RABAO);
        CHECK_EQ(zt().mode, 5); CHECK_EQ(zt().segLastRun, 1);
        deliver(pkt_zone_msg(7249, 13, 1013));
        CHECK_EQ(zt().segments, 13);
    }

    SECTION("zone tracker 0x02A limbus : a coffer award banks the units and records its quadrant ; a point of interest does not");
    {   // Mutation : the chip no longer gated on the source's NAME (`p1 >= LIMBUS_COFFER_MIN && isCoffer` -> `p1 >=
        // LIMBUS_COFFER_MIN`) -- the false-chip bug : a 1000-unit point of interest painted a quadrant red.
        // Mutation : the source index read from the message id's bytes (`p[0x18]|p[0x19]` -> `p[0x1A]|p[0x1B]`) -- the
        // first captures' '<unresolved>', and no coffer ever recorded.
        fresh();
        entity(0x123, 0x0100F123u, "Apollyon Coffer #4");
        entity(0x124, 0x0100F124u, "???");
        enter(Z_APOLLYON);
        CHECK_EQ(zt().mode, 6);
        deliver(pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 50, "SW_Floor_#4" } }));
        deliver(pkt_zone_msg(7247, 3000, 0, 15000, 100000, 0x123));      // "Acquired Apollyon units: 3000 ... Total: 15000/100000"
        CHECK_EQ(zt().limbusUnits, 15000); CHECK_EQ(zt().limbusUnitsCap, 100000);
        CHECK_EQ(zt().limbusRunUnits, 3000);
        CHECK_EQ(zt().limbusCofferAmt, 3000); CHECK_STR(zt().limbusCofferAt, "SW #4");
        CHECK_EQ(party().limbus_coffers(0).slotK[1], 3);                 // SW = slot 1 : a red 3k
        deliver(pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 10, "NW_Floor_#5" } }));
        deliver(pkt_zone_msg(7247, 1000, 0, 16000, 100000, 0x124));      // the same id, from '???'
        CHECK_EQ(zt().limbusRunUnits, 4000);                             // units are units, whoever paid them
        CHECK_EQ(zt().limbusCofferAmt, 3000);                            // ...but no coffer
        CHECK_EQ(party().limbus_coffers(0).slotK[0], 0);                 // NW stays unopened
        CHECK_EQ(party().limbus_coffers(0).slotK[1], 3);
    }

    SECTION("zone tracker 0x02A limbus : the weekly allowance (id 7280) is kept, and a bad count is clamped");
    {   // Mutation : only the assumed id 7288 accepted (`msg == 7280 || msg == 7288` -> `msg == 7288`) -- the counter that
        // was never filled until the 2026-07-19 capture.
        fresh(); enter(Z_APOLLYON);
        CHECK_EQ(party().limbus_runs_left(), -1);                        // never observed : unknown, not five
        deliver(pkt_zone_msg(7280, 3));
        CHECK_EQ(party().limbus_runs_left(), 3);
        deliver(pkt_zone_msg(7280, 9));
        CHECK_EQ(party().limbus_runs_left(), 5);
    }

    SECTION("zone tracker 0x02A limbus : Temenos pays on its own id (7239), not on Apollyon's");
    {   // Mutation : the wings swapped (`(zt_.curZone == 37) ? 1 : 0` -> `(zt_.curZone == 38) ? 1 : 0`) -- a Temenos run
        // then listens for Apollyon's id and records nothing.
        fresh();
        entity(0x130, 0x0100F130u, "Temenos Coffer #2");
        enter(Z_TEMENOS);
        deliver(pkt_battlefield({ { -1, "Temenos_Lv135" }, { 20, "West_Tower_F4" } }));
        deliver(pkt_zone_msg(7247, 3000, 0, 9000, 100000, 0x130));       // Apollyon's award id, heard in Temenos
        CHECK_EQ(zt().limbusUnits, -1);
        deliver(pkt_zone_msg(7239, 3000, 0, 9000, 100000, 0x130));
        CHECK_EQ(zt().limbusUnits, 9000);
        CHECK_EQ(party().limbus_coffers(1).slotK[1], 3);                 // W = Temenos slot 1
        CHECK_EQ(party().limbus_coffers(0).slotK[1], 0);                 // Apollyon's row untouched
    }

    // ================================================ 0x055 ================================================
    SECTION("zone tracker 0x055 dynamis : owned granules extend the run limit ; another key-item table does not");
    {   // Mutation : the table check dropped (`if (pkt_u32(p, 0x84) != 3) return;`) -- table 2's bits 9..13 are other key
        // items, and read as granules they would add time the run does not have.
        // Mutation : the granule bits off by one (`bit = 9 + i` -> `8 + i`).
        fresh(); enter(Z_DYN_SANDORIA);
        CHECK_EQ(zt().mode, 1); CHECK_EQ(zt().dynLimitSec, 3600);
        deliver(pkt_key_items(3, { 1545, 1549 }));                       // Crimson (+10) and Obsidian (+15 in a city)
        CHECK_EQ(zt().ki[0], 1); CHECK_EQ(zt().ki[1], 0); CHECK_EQ(zt().ki[2], 0); CHECK_EQ(zt().ki[3], 0); CHECK_EQ(zt().ki[4], 1);
        CHECK_EQ(zt().dynLimitSec, 3600 + 600 + 900);
        deliver(pkt_key_items(2, { 1033, 1034, 1035, 1036, 1037 }));     // bits 9..13 of table 2
        CHECK_EQ(zt().ki[1], 0);
        CHECK_EQ(zt().dynLimitSec, 5100);
        Packet p = pkt_key_items(3, { 1545, 1546, 1547, 1548, 1549 });
        pkt_truncate(p, 0x84);                                           // the table number is past the declared end
        deliver(p);
        CHECK_EQ(zt().dynLimitSec, 5100);
    }

    SECTION("zone tracker 0x055 : the Nyzul armband counts in the staging point only, and Divergence has no granules");
    {   // Mutation : ny_has_ki's table check loosened (`ty != table` -> `ty > table`) -- key item 285 of table 0 would
        // read as the armband (797 = table 1, bit 285) and inflate the token estimate by 10 %.
        // Mutation : the staging-point filter dropped (`zt_.curZone == 72 &&`).
        // Mutation : the Divergence guard dropped (`if (zt_is_divergence(zt_.dynZone)) return;`) -- five red dots all run.
        fresh(); enter(Z_ALZADAAL);
        deliver(pkt_key_items(0, { 285 }));
        CHECK_EQ(zt().nyArmband, 0);
        deliver(pkt_key_items(1, { 797 }));
        CHECK_EQ(zt().nyArmband, 1);
        fresh();                                                         // a town
        deliver(pkt_key_items(1, { 797 }));
        CHECK_EQ(zt().nyArmband, 0);
        fresh(); enter(Z_DIV_SANDORIA);
        CHECK_EQ(zt().mode, 1);
        deliver(pkt_key_items(3, { 1545, 1546, 1547, 1548, 1549 }));
        CHECK_EQ(zt().ki[0] + zt().ki[1] + zt().ki[2] + zt().ki[3] + zt().ki[4], 0);
        CHECK_EQ(zt().dynLimitSec, 3600);
    }

    // ================================================ 0x118 ================================================
    SECTION("zone tracker 0x118 : currency2 gives the Mog Segments and both Limbus totals, each from its own field");
    {   // Mutation : Temenos read from Apollyon's field (`pkt_u32(p, 0x98)` -> `pkt_u32(p, 0x9C)`).
        fresh();
        deliver(pkt_currency2(947485, 12000, 34000));
        CHECK_EQ(zt().segBank, 947485);
        CHECK_EQ(zt().limbusTemenos, 12000);
        CHECK_EQ(zt().limbusApollyon, 34000);
    }

    SECTION("zone tracker 0x118 : a truncated currency2 gives nothing (still 'never seen', not a number)");
    {   // Mutation : the size floor lowered (`< 0xA0` -> `< 0x9C`) -- the Apollyon total would be read past the end.
        fresh();
        Packet p = pkt_currency2(947485, 12000, 34000);
        pkt_truncate(p, 0x9C);
        deliver(p);
        CHECK_EQ(zt().segBank, -1);
        CHECK_EQ(zt().limbusTemenos, -1);
        CHECK_EQ(zt().limbusApollyon, -1);
    }

    // ================================================ 0x034 ================================================
    SECTION("zone tracker 0x034 : the Rabao conflux menu names Sheol A/B/C, and the choice carries into the run");
    {   // Mutation : the choice read from the next parameter (`pkt_u32(p, 0x08)` -> `pkt_u32(p, 0x0C)`).
        fresh(); enter(Z_RABAO);
        deliver(pkt_npc_menu(ME, 173, 2));
        CHECK_EQ(zt().sheolzone, 2);
        enter(Z_SHEOL);
        CHECK_EQ(zt().mode, 5);
        CHECK_EQ(zt().sheolzone, 2);
        enter(Z_RABAO);                                                  // the run ends : A/B/C cleared for the next one
        CHECK_EQ(zt().sheolzone, 0);
        deliver(pkt_npc_menu(ME, 173, 4));                               // Gaol (param 4, measured 2026-09-09)
        CHECK_EQ(zt().sheolzone, 4);
    }

    SECTION("zone tracker 0x034 : another menu, another player, another zone or an unknown parameter names nothing");
    {   // Mutation : the menu id check dropped (`if (pkt_u16(p, 0x2C) != 173) return;`) -- any Rabao NPC's first menu
        // parameter would become a Sheol.
        // Mutation : the actor check dropped (`!= selfId_`) ; the Rabao check dropped (`curZone != 247`) ; the parameter
        // range widened (`i > 0 && i < 5` -> `i >= 0 && i < 8`).
        fresh(); enter(Z_RABAO);
        deliver(pkt_npc_menu(ME, 174, 1));
        CHECK_EQ(zt().sheolzone, 0);
        deliver(pkt_npc_menu(KAO, 173, 1));                              // a party member at the conflux, not you
        CHECK_EQ(zt().sheolzone, 0);
        deliver(pkt_npc_menu(ME, 173, 7));
        CHECK_EQ(zt().sheolzone, 0);
        deliver(pkt_npc_menu(ME, 173, 0));
        CHECK_EQ(zt().sheolzone, 0);
        fresh();                                                         // menu 173 somewhere other than Rabao
        deliver(pkt_npc_menu(ME, 173, 1));
        CHECK_EQ(zt().sheolzone, 0);
    }

    // ================================================ 0x075 ================================================
    SECTION("zone tracker 0x075 limbus : Apollyon's bars give the wing, level, quadrant, floor and gauge");
    {   // Mutation : the gauge taken as sent (`(prog >= 0 && prog <= 100) ? prog : -1` -> `prog`) -- the 0x7FFFFFFF
        // sentinel of an inactive gauge drawn as a full bar.
        // Mutation : the bar table 4 bytes early (`0x28 + i * 0x14` -> `0x24 + i * 0x14`).
        fresh(); enter(Z_APOLLYON);
        CHECK_EQ(zt().limbusFloor, -1); CHECK_EQ(zt().limbusProgress, -1);
        // The 2026-07-18 capture : bar0 the label carrier, bar1 the floor AND the gauge, bar2 Uniq_Data, the rest unused.
        deliver(pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 96, "SW_Floor_#3" }, { 0, "Uniq_Data0" } }));
        CHECK_STR(zt().limbusArea, "Apollyon"); CHECK_EQ(zt().limbusLevel, 119);
        CHECK_STR(zt().limbusQuad, "SW"); CHECK_EQ(zt().limbusFloor, 3);
        CHECK_EQ(zt().limbusProgress, 96);
        deliver(pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 0x7FFFFFFF, "SW_Floor_#3" } }));
        CHECK_EQ(zt().limbusProgress, -1);                               // inactive, not 2147483647 %
        CHECK_EQ(zt().limbusFloor, 3);
    }

    SECTION("zone tracker 0x075 limbus : Temenos spells its towers out ('North_Tower_F1'), up to a full 16-byte label");
    {   // Mutation : the "Tower" alternative no longer matching (`lbl[k]=='T'` -> `lbl[k]=='F'`), i.e. only "Floor" --
        // the reported "Temenos shows the name but never a progress bar".
        fresh(); enter(Z_TEMENOS);
        deliver(pkt_battlefield({ { -1, "Temenos_Lv135" }, { 40, "North_Tower_F1" } }));
        CHECK_STR(zt().limbusArea, "Temenos"); CHECK_EQ(zt().limbusLevel, 135);
        CHECK_STR(zt().limbusQuad, "North"); CHECK_EQ(zt().limbusFloor, 1); CHECK_EQ(zt().limbusProgress, 40);
        deliver(pkt_battlefield({ { -1, "Temenos_Lv135" }, { 75, "Central_Tower_F4" } }));   // 16 chars : no terminator in the packet
        CHECK_STR(zt().limbusQuad, "Central"); CHECK_EQ(zt().limbusFloor, 4); CHECK_EQ(zt().limbusProgress, 75);
    }

    SECTION("zone tracker 0x075 : outside Limbus, a short packet, or unlabelled bars are not adopted");
    {   // Mutation : the mode gate dropped (`if (zt_.mode != 6) return;`) -- 0x075 is multiplexed, any battlefield anywhere
        // would write a Limbus floor.
        // Mutation : the size floor lowered (`< 0x9C` -> `< 0x98`).
        // (Lowering the label-length floor `n < 3` to `n < 1` is an EQUIVALENT mutant : no label under 3 characters can
        // hold "_Lv", "Floor" or "Tower", so nothing observable depends on it.)
        fresh();
        deliver(pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 96, "SW_Floor_#3" } }));
        CHECK_STR(zt().limbusArea, ""); CHECK_EQ(zt().limbusFloor, -1); CHECK_EQ(zt().limbusProgress, -1);
        enter(Z_APOLLYON);
        Packet p = pkt_battlefield({ { -1, "Apollyon_Lv119" }, { 96, "SW_Floor_#3" } });
        pkt_truncate(p, 0x98);
        deliver(p);
        CHECK_STR(zt().limbusArea, ""); CHECK_EQ(zt().limbusFloor, -1);
        deliver(pkt_battlefield({ { 50, "" }, { 60, "\x01\x02" }, { 70, "ab" } }));   // position floats, not bars
        CHECK_EQ(zt().limbusFloor, -1); CHECK_EQ(zt().limbusProgress, -1); CHECK_STR(zt().limbusQuad, "");
    }

    SECTION("zone tracker 0x075 sheol : the Gaol countdown is read, and a non-Odyssey battlefield is not");
    {   // Mutation : the instance filter dropped (`inst >= 1019 && inst <= 1025 && sec > 0` -> `sec > 0`).
        fresh(); enter(Z_RABAO); enter(Z_SHEOL);
        deliver(pkt_battlefield({}, 1025, 0, 898));                      // measured in Gaol : @04 = 1025, @0C = 898 s
        CHECK_EQ(zt().gaolSec, 898);
        CHECK_EQ(zt().sheolzone, 4);                                     // a Gaol packet names Gaol
        deliver(pkt_battlefield({}, 65535, 779329666, 1878));            // another sender's words in the same bytes
        CHECK_EQ(zt().gaolSec, 898);
    }

    // ================================================ 0x02D ================================================
    SECTION("pointwatch 0x02D : XP, CP, Limit and Exemplar gains move their counters and roll over");
    {   // Mutation : the gain read at 0x029's offset for both ids (`id == 0x029 ? 0x0C : 0x10` -> `0x0C`) -- a 0x02D
        // gain would read the entity indices instead.
        // Mutation : the level rollover tested against the clamp (`xpCur > xpTnl` -> `xpCur > 55999u`).
        // Mutation : the merit rollover dropped (`merits += lpCur / 10000` -> `merits += 0`).
        fresh();
        pointwatch_memory(1000, 8000, 5000, 100000, 9500, 3, 30);
        frame();                                                         // the memory block fills the rows
        CHECK_EQ(party().pointwatch().xpCur, 1000u); CHECK_EQ(party().pointwatch().xpTnl, 8000u);
        // No frame() below : the next one re-reads the memory block, which is what the game does too.
        deliver(pkt_exp_msg(8, 300));
        CHECK_EQ(party().pointwatch().xpCur, 1300u);
        CHECK_EQ(party().pointwatch().xpReg.n, 1);
        deliver(pkt_exp_msg(105, 7000));                                 // 8300 of 8000 : a level, 300 into the next
        CHECK_EQ(party().pointwatch().xpCur, 300u);
        deliver(pkt_exp_msg(809, 2000));
        CHECK_EQ(party().pointwatch().epCur, 7000u); CHECK_EQ(party().pointwatch().epReg.n, 1);
        deliver(pkt_exp_msg(718, 29500));
        deliver(pkt_exp_msg(735, 1000));                                 // 30500 : one Job Point, 500 left
        CHECK_EQ(party().pointwatch().cpJp, 1); CHECK_EQ(party().pointwatch().cpCur, 500u);
        CHECK_EQ(party().pointwatch().cpReg.n, 2);
        deliver(pkt_exp_msg(371, 400));
        CHECK_EQ(party().pointwatch().lpCur, 9900u);
        deliver(pkt_exp_msg(372, 400));                                  // 10300 : a fourth merit, 300 left
        CHECK_EQ(party().pointwatch().merits, 4); CHECK_EQ(party().pointwatch().lpCur, 300u);
    }

    SECTION("pointwatch 0x02D : an unrelated message, a zero gain or a truncated packet moves nothing");
    {   // Mutation : the size floor lowered (`< 0x1A` -> `< 0x18`) -- the message id would be read past the end.
        // Mutation : an unrelated message counted as XP (`msg == 8 || msg == 105` -> `... || msg == 6`).
        fresh();
        pointwatch_memory(1000, 8000);
        frame();
        deliver(pkt_exp_msg(6, 500));                                    // "defeats the <mob>" : a message, not a gain
        deliver(pkt_exp_msg(8, 0));
        Packet p = pkt_exp_msg(8, 500);
        pkt_truncate(p, 0x18);
        deliver(p);
        CHECK_EQ(party().pointwatch().xpCur, 1000u);
        CHECK_EQ(party().pointwatch().xpReg.n, 0);
        CHECK_EQ(party().pointwatch().cpCur, 0u); CHECK_EQ(party().pointwatch().lpCur, 0u); CHECK_EQ(party().pointwatch().epCur, 0u);
    }

    // ================================================ 0x01B ================================================
    SECTION("job info 0x01B : the encumbrance flags are adopted, zero included ; a short packet keeps the last ones");
    {   // Mutation : the flags read one field early (`pkt_u32(p, 0x60)` -> `pkt_u32(p, 0x5C)`).
        // Mutation : the size floor lowered (`< 0x64` -> `< 0x60`).
        fresh();
        CHECK_EQ(party().encumbrance(), 0u);
        deliver(pkt_job_info(0x00008001u));                              // main (bit 0) and back (bit 15) locked
        CHECK_EQ(party().encumbrance(), 0x00008001u);
        Packet p = pkt_job_info(0x0000FFFFu);
        pkt_truncate(p, 0x60);
        deliver(p);
        CHECK_EQ(party().encumbrance(), 0x00008001u);
        deliver(pkt_job_info(0));                                        // nothing locked any more IS an answer
        CHECK_EQ(party().encumbrance(), 0u);
    }

    // ================================================ 0x068 ================================================
    SECTION("hate list 0x068 : a party member's pet and the mob it fights join the hate list, under the owner's name");
    {   // Mutation : the target read one field early (`pkt_u32(p, 0x14)` -> `pkt_u32(p, 0x10)`).
        // Mutation : pet id and owner swapped in register_pet.
        fresh();
        entity(0x700, 0x01000701u, "Carbuncle", 0x02);
        entity(0x123, 0x01001234u, "Crawler", 0x10, 80, 1);
        deliver(pkt_pet_status(GAB, 0x700, 0x01001234u));
        CHECK(party().is_party_or_pet(0x01000701u));                     // a mob swinging at Carbuncle is aggro on the party
        CHECK_EQ(party().pet_owner(0x01000701u), GAB);
        frame();
        int n = 0; const HateRow* r = party().hate_rows(n);
        CHECK_EQ(n, 1);
        if (n == 1) { CHECK_EQ(r[0].id, 0x01001234u); CHECK_STR(r[0].mob, "Crawler"); CHECK_STR(r[0].pc, "Gab"); CHECK_EQ(r[0].hpp, 80); }
    }

    SECTION("hate list 0x068 : a stranger's pet, a pet fighting a player, or a short packet adds nothing");
    {   // Mutation : the owner filter dropped (`!ownerId || !is_party_or_pet(ownerId)` -> `!ownerId`) -- every pet in the
        // zone would feed the list.
        // Mutation : the mob-id filter dropped (`tgt >= 0x01000000u` -> `tgt`) ; the size floor lowered (`< 0x18` -> `< 0x14`).
        fresh();
        entity(0x700, 0x01000701u, "Carbuncle", 0x02);
        entity(0x701, 0x01000702u, "Ifrit", 0x02);
        entity(0x123, 0x01001234u, "Crawler", 0x10, 80, 1);
        deliver(pkt_pet_status(0x00099999u, 0x701, 0x01001234u));       // not in the party
        CHECK(!party().is_party_or_pet(0x01000702u));
        Packet p = pkt_pet_status(GAB, 0x700, 0x01001234u);
        pkt_truncate(p, 0x14);                                           // the target id is past the declared end
        deliver(p);
        CHECK(!party().is_party_or_pet(0x01000701u));
        deliver(pkt_pet_status(GAB, 0x700, KAO));                        // a PC id as the target : the pet is learnt, no hate
        CHECK(party().is_party_or_pet(0x01000701u));
        frame();
        int n = 0; party().hate_rows(n);
        CHECK_EQ(n, 0);
        int tracked = 0; for (int i = 0; i < 128; ++i) if (party().hate_[i].mob) ++tracked;
        CHECK_EQ(tracked, 0);
    }
}
