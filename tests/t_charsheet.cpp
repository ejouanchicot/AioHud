// t_charsheet.cpp -- the character sheet decoded out of three packets the plugin already receives
// (model/charsheet.h). Each decoder is fed a packet built byte for byte in the layout it walks, and is also
// fed the packet it must REFUSE : one byte too short.
//
// These three packets were read for six fields and carry more than fifty. The risk in taking the rest is not
// that a number comes out wrong -- it is that a number comes out of the wrong OFFSET and still looks plausible
// (an attribute is a small integer, and so is the one four bytes further on). So every case here checks values
// that cannot be confused with their neighbours: each field gets a distinct number, and the test names it.
#include "check.h"
#include "model/charsheet.h"
#include <cstring>

using namespace aio;

namespace {
// A 0x061 built at the layout charsheet.h reads (Windower fields.lua). Distinct values everywhere, so a field
// read one slot off shows up as the neighbour's number rather than as something plausible.
void build_061(unsigned char* b, int bytes) {
    memset(b, 0, 256);
    b[0] = (unsigned char)((0x061) & 0xFF);
    b[1] = (unsigned char)(((0x061 >> 8) & 1) | ((bytes / 4) << 1));   // id 9 bits | size in dwords
    auto u16=[&](int o, unsigned v){ b[o]=(unsigned char)v; b[o+1]=(unsigned char)(v>>8); };
    auto u32=[&](int o, unsigned v){ for (int i=0;i<4;++i) b[o+i]=(unsigned char)(v>>(8*i)); };
    u32(0x04, 2980); u32(0x08, 1077);                       // max HP / MP
    b[0x0C]=7; b[0x0D]=99; b[0x0E]=16; b[0x0F]=58;          // PLD 99 / BLU 58
    u16(0x10, 55999); u16(0x12, 56000);                     // EXP
    const unsigned base[7]={112,101,113,94,88,104,92};
    for (int a=0;a<7;++a) u16(0x14+a*2, base[a]);
    const int add[7]={87,63,94,41,-5,72,38};                // one NEGATIVE : gear can subtract
    for (int a=0;a<7;++a) u16(0x22+a*2, (unsigned)(short)add[a]);
    u16(0x30, 1112); u16(0x32, 1246);                       // attack / defense
    const int res[8]={72,68,65,81,64,70,88,-9};             // one negative resistance too
    for (int e=0;e<8;++e) u16(0x34+e*2, (unsigned)(short)res[e]);
    u16(0x44, 512); u16(0x46, 10); u16(0x48, 3210);         // title / nation rank / rank points
    b[0x65]=48;                                             // master level (read by the PointWatch path)
}

void build_01b(unsigned char* b, int bytes) {
    memset(b, 0, 256);
    b[0]=0x1B; b[1]=(unsigned char)((bytes/4)<<1);
    b[0x0C]=0x01; b[0x0D]=0x40;                             // unlock flags : subjob + one job bit
    for (int j=1;j<23;++j) b[0x49+(j-1)] = (unsigned char)(j==7?99: j==16?58: j==17?99: j);  // PLD 99, BLU 58, COR 99
    // THE MASTERY FLAGS ARE BITS. This fixture used to write them as BYTES, which is exactly how the decoder
    // read them -- so the test agreed with the bug and could never see it. Only the second witness (Windower
    // parsing the same live packet) caught it: eleven jobs mastered, four of them read correctly. A fixture
    // written from the code instead of from the packet's own layout is not a test, it is an echo.
    // fields.lua : bit[1] _junk1, then 22 boolbits -> the flag of job id j is bit j of the field at 0x68.
    auto master=[&](int j){ b[0x68+(j>>3)] |= (unsigned char)(1u << (j & 7)); };
    master(7);                                              // PLD mastered
    master(17);                                             // COR mastered
    b[0x6D+(7-1)]=48;                                       // ...and their Master Levels, 22 bytes at 0x6D
    b[0x6D+(17-1)]=12;
    b[0x60]=0x05;                                           // encumbrance, untouched by this decoder
}

void build_063_o5(unsigned char* b, int bytes) {
    memset(b, 0, 256);
    b[0]=0x63; b[1]=(unsigned char)((bytes/4)<<1);
    b[0x04]=5;                                              // order 5
    auto u16=[&](int o, unsigned v){ b[o]=(unsigned char)v; b[o+1]=(unsigned char)(v>>8); };
    for (int j=1;j<23;++j){ const int e=0x0C+j*6;
        u16(e, (unsigned)(1000+j));        // Capacity Points
        u16(e+2, (unsigned)(j*10));        // Job Points in RESERVE (the game caps this at 500)
        u16(e+4, (unsigned)(j*100)); }     // Job Points SPENT -- a different number, in a different place
}
}  // namespace

void test_charsheet() {

    SECTION("charsheet 0x061 : the fifty fields nobody was reading, each from its own offset");
    {
        unsigned char b[256]; build_061(b, 0x70);
        CharSheet cs;
        CHECK(cs_read_061(b, 0x70, cs));
        CHECK(cs.statsOk);
        CHECK_EQ((int)cs.maxHp, 2980); CHECK_EQ((int)cs.maxMp, 1077);
        CHECK_EQ((int)cs.mjob, 7); CHECK_EQ((int)cs.mlvl, 99);
        CHECK_EQ((int)cs.sjob, 16); CHECK_EQ((int)cs.slvl, 58);
        CHECK_EQ((int)cs.xpCur, 55999); CHECK_EQ((int)cs.xpNext, 56000);
        // The attributes : base and what the gear adds are two SEPARATE blocks 14 bytes apart. Read one for the
        // other and every number is still plausible -- which is exactly why each has its own value here.
        CHECK_EQ((int)cs.base[CS_STR], 112); CHECK_EQ((int)cs.base[CS_CHR], 92);
        CHECK_EQ((int)cs.added[CS_STR], 87); CHECK_EQ((int)cs.added[CS_CHR], 38);
        CHECK_EQ(cs.attr_total(CS_VIT), 113 + 94);
        // A NEGATIVE bonus must stay negative : the block is signed, and read unsigned it would come out at
        // 65531 and inflate the total instead of lowering it.
        CHECK_EQ((int)cs.added[CS_INT], -5);
        CHECK_EQ(cs.attr_total(CS_INT), 83);
        CHECK_EQ((int)cs.attack, 1112); CHECK_EQ((int)cs.defense, 1246);
        CHECK_EQ((int)cs.resist[CS_FIRE], 72); CHECK_EQ((int)cs.resist[CS_LIGHT], 88);
        CHECK_EQ((int)cs.resist[CS_DARK], -9);              // signed here too
        CHECK_EQ((int)cs.title, 512); CHECK_EQ((int)cs.nationRank, 10); CHECK_EQ((int)cs.rankPoints, 3210);
    }
    {   // One byte short of the highest field it reads : refused whole, and nothing is claimed.
        // Mutation : `bytes < 0x4A` -> `bytes < 0x48` (the rank points then come from whatever followed).
        unsigned char b[256]; build_061(b, 0x70);
        CharSheet cs;
        CHECK(!cs_read_061(b, 0x49, cs));
        CHECK(!cs.statsOk);
        CHECK_EQ((int)cs.attack, 0);
        CHECK(!cs_read_061(0, 0x70, cs));                   // and a null pointer is not a packet
    }

    SECTION("charsheet 0x01B : the level of all 22 jobs, and which are mastered");
    {
        unsigned char b[256]; build_01b(b, 0x84);   // a real 0x01B is 132 bytes (measured) -- the master levels end at 0x82, so it just fits
        CharSheet cs;
        CHECK(cs_read_01b(b, 0x84, cs));
        CHECK(cs.jobsOk);
        CHECK_EQ((int)cs.jobLvl[7], 99);                    // PLD
        CHECK_EQ((int)cs.jobLvl[16], 58);                   // BLU
        CHECK_EQ((int)cs.jobLvl[17], 99);                   // COR
        CHECK_EQ((int)cs.jobLvl[1], 1);                     // WAR : the table is indexed by job id, 1-based
        CHECK(cs.has_job(7) && cs.has_job(17));
        CHECK(!cs.has_job(0));                              // there is no job 0, and asking must not read before the table
        CHECK_EQ((int)cs.mastered[7], 1);
        CHECK_EQ((int)cs.mastered[17], 1);
        CHECK_EQ((int)cs.mastered[16], 0);                  // BLU is levelled but not mastered
        CHECK(cs.masteredOk);
        CHECK_EQ((int)cs.masterLvl[7], 48);                 // the Master Levels are in THIS packet, at 0x6D
        CHECK_EQ((int)cs.masterLvl[17], 12);
        CHECK_EQ((int)cs.masterLvl[16], 0);
        CHECK_EQ((int)(cs.unlockFlags & 1), 1);             // subjob unlocked
    }
    {   // THREE BLOCKS, THREE FLOORS. A short 0x01B must give up everything it really carries and nothing more.
        // One floor for the whole packet would throw away a table the packet does hold -- which is what the old
        // single 0x7E floor did to the levels of any packet shorter than the mastery block.
        // Mutation : put back one floor for the packet, at any of the three values, and one of these fails.
        unsigned char b[256]; build_01b(b, 0x70);
        CharSheet cs;
        CHECK(cs_read_01b(b, 0x70, cs));                    // long enough for the levels AND the mastery bits
        CHECK(cs.jobsOk);
        CHECK(cs.masteredOk);
        CHECK_EQ((int)cs.jobLvl[7], 99);
        CHECK_EQ((int)cs.masterLvl[7], 0);                  // ...but NOT for the master levels : left at zero
    }
    {   // Long enough for the levels, too short for the mastery bits.
        unsigned char b[256]; build_01b(b, 0x60);
        CharSheet cs;
        CHECK(cs_read_01b(b, 0x60, cs));
        CHECK(cs.jobsOk);
        CHECK_EQ((int)cs.jobLvl[7], 99);
        CHECK(!cs.masteredOk);                              // the flag has NO source : it must not read as "not mastered"
        CHECK_EQ((int)cs.mastered[7], 0);
    }
    {   // Too short even for the levels : nothing at all, and nothing half-filled.
        unsigned char b[256]; build_01b(b, 0x50);
        CharSheet cs;
        CHECK(!cs_read_01b(b, 0x50, cs));
        CHECK(!cs.jobsOk);
        CHECK_EQ((int)cs.jobLvl[7], 0);
    }

    SECTION("charsheet 0x063 order 5 : Capacity and Job Points of EVERY job, not just the one you play");
    {
        unsigned char b[256]; build_063_o5(b, 0xA0);
        CharSheet cs;
        CHECK(cs_read_063_jobpoints(b, 0xA0, cs));
        CHECK(cs.pointsOk);
        CHECK_EQ((int)cs.cp[1], 1001);  CHECK_EQ((int)cs.jpHeld[1], 10);   CHECK_EQ((int)cs.jpSpent[1], 100);
        CHECK_EQ((int)cs.cp[7], 1007);  CHECK_EQ((int)cs.jpHeld[7], 70);   CHECK_EQ((int)cs.jpSpent[7], 700);
        CHECK_EQ((int)cs.cp[22], 1022); CHECK_EQ((int)cs.jpHeld[22], 220); CHECK_EQ((int)cs.jpSpent[22], 2200);
    }
    {   // A TABLE THAT STOPS EARLY IS KEPT UP TO WHERE IT STOPS. The packet is real, its entries are real, and
        // the jobs past the end simply have no answer -- far better than refusing the whole thing or reading on.
        // Mutation : `if (bytes < e + 4) break;` removed -> the last jobs come from whatever follows the packet.
        unsigned char b[256]; build_063_o5(b, 0x40);
        CharSheet cs;
        CHECK(cs_read_063_jobpoints(b, 0x40, cs));
        CHECK(cs.pointsOk);
        CHECK_EQ((int)cs.cp[7], 1007);                      // job 7 ends at 0x0C+7*6+6 = 0x3C : inside
        CHECK_EQ((int)cs.cp[22], 0);                        // job 22 would end at 0x90 : never touched
    }
    {   // THE DEFECT THE CROSS-CHECK FOUND, 2026-09-15. The six-byte entry holds THREE numbers and the middle one
        // had been called "spent" for months : where Windower reported 2100 job points spent, the plugin read
        // 500 -- the cap on points HELD -- and where Windower reported 0 spent it read 73, 23, 2, the reserves
        // of jobs never merited. Nothing contradicted it until a second witness was asked.
        // Mutation : read jpSpent from e+2 -> the reserve is reported as the lifetime total.
        unsigned char b[256]; build_063_o5(b, 0xA0);
        CharSheet cs;
        CHECK(cs_read_063_jobpoints(b, 0xA0, cs));
        CHECK(cs.jpHeld[7] != cs.jpSpent[7]);          // two DIFFERENT numbers, or the offsets collapsed
        CHECK_EQ((int)cs.jpHeld[7], 70);
        CHECK_EQ((int)cs.jpSpent[7], 700);
    }
    {   // Another order in the same packet id is not ours -- and is not a malformed packet either.
        // Mutation : the `cs_u16(p,4) != 5` guard removed -> a merit packet fills the job-point table with
        // whatever sits at those offsets, and every job gets numbers nobody measured.
        unsigned char b[256]; build_063_o5(b, 0xA0);
        b[0x04] = 2;                                        // order 2 : merits
        CharSheet cs;
        CHECK(!cs_read_063_jobpoints(b, 0xA0, cs));
        CHECK(!cs.pointsOk);
        CHECK_EQ((int)cs.cp[7], 0);
    }
}
