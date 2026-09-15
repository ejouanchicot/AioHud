// charsheet.h -- everything the client TELLS US about the character, decoded from packets we already receive.
//
// WHY THIS FILE EXISTS. Three packets arrive on every login, job change and zone, and the plugin read six fields
// out of them : EXP, Master Level, Exemplar, the merit count, the main job's Capacity/Job Points and the
// encumbrance bitfield. The rest -- the seven attributes with what the gear adds, Attack, Defense, the eight
// resistances, the title and rank, the level of all 22 jobs, which are unlocked, which are MASTERED, and the
// Capacity/Job Points of every job, not just the one you are playing -- was in the same bytes, unread.
// Nothing here is reverse-engineered : the layouts are Windower's own `addons/libs/packets/fields.lua`, the
// same source the encumbrance offset was taken from, cross-checked against the values the game shows.
//
// PURE DECODERS. Each takes the packet bytes and the DECLARED length, and returns false rather than reading one
// byte past what the server sent. They touch no global and no clock, so tests/t_charsheet.cpp drives them with
// bit-exact packets (tests/fake_packets.h) instead of a live game.
//
// WHAT IS NOT HERE, and why : the merits SPENT PER CATEGORY ("Great Sword +4", "Ice Potency") and the list of
// spells/abilities KNOWN. Neither is in these packets -- the first lives in the Merit Points menu, the second in
// a client-side table -- so both would be real reverse-engineering, not more decoding. Saying so is the point:
// an empty field with a reason beats a zero that looks like a measurement.
#pragma once

namespace aio {

// The seven attributes, in the order the game's own status menu lists them.
enum { CS_STR = 0, CS_DEX, CS_VIT, CS_AGI, CS_INT, CS_MND, CS_CHR, CS_ATTR_N };
// The eight elements, in the game's own order (the one the resistance block uses).
enum { CS_FIRE = 0, CS_ICE, CS_WIND, CS_EARTH, CS_LIGHTNING, CS_WATER, CS_LIGHT, CS_DARK, CS_ELEM_N };
// Job ids run 1..22 (WAR..RUN). Index 0 is "no job" and stays empty, so a job id indexes these arrays directly.
const int CS_JOB_N = 23;

struct CharSheet {
    // ---- from 0x061 "Char Stats" ----------------------------------------------------------------
    bool           statsOk = false;      // a 0x061 was decoded at least once (false = nothing below is a measurement)
    // ...and WHERE it came from. The memory mirror starts at the packet's 0x10, so it can fill the attributes
    // but NEVER maxHP / maxMP / the jobs, which live before it. Without this flag those three read as a
    // measured zero, and the cross-check compared them against Windower and called the plugin wrong.
    bool           fromPacket = false;
    unsigned       maxHp = 0, maxMp = 0;
    unsigned char  mjob = 0, mlvl = 0, sjob = 0, slvl = 0;
    unsigned short xpCur = 0, xpNext = 0;
    unsigned short base[CS_ATTR_N] = {};   // what the character has naked
    short          added[CS_ATTR_N] = {};  // what the gear adds -- SIGNED : a piece can subtract
    unsigned short attack = 0, defense = 0;
    short          resist[CS_ELEM_N] = {};
    unsigned short title = 0, nationRank = 0, rankPoints = 0;

    // ---- from 0x01B "Job Info" ------------------------------------------------------------------
    bool           jobsOk = false;       // the job LEVELS are usable (from the packet, or from the client's own table)
    // The mastery FLAG comes only from the packet : memory carries the master LEVEL beside the job levels, and
    // "master level 0" is not the same fact as "not mastered" (a job just mastered sits at ML 0). Deriving one
    // from the other would have invented a fact -- so the flag keeps its own validity bit.
    bool           masteredOk = false;
    unsigned char  jobLvl[CS_JOB_N] = {};    // 1 = unlocked, never levelled ; 0 = not unlocked (the game stores 1, not 0)
    unsigned char  mastered[CS_JOB_N] = {};  // 1 = mastered (only meaningful once the Master Breaker KI is held)
    unsigned char  masterLvl[CS_JOB_N] = {}; // Master Level per job (0..50). From memory ; the packet path leaves it 0.
    unsigned       unlockFlags = 0;      // bit 0 = subjob unlocked, then one per job

    // ---- from 0x063 order 5 "Job Points" ---------------------------------------------------------
    bool           pointsOk = false;
    unsigned short cp[CS_JOB_N] = {};       // Capacity Points held, per job
    // THE SIX-BYTE ENTRY HOLDS THREE NUMBERS, and the middle one is not what the code called it. Measured
    // 2026-09-15 against Windower : where it reports 2100 job points SPENT we read 500 at +2, and 500 is the
    // cap on job points HELD ; where it reports 0 spent we read 73, 23, 2 -- jobs with a few points in reserve
    // and nothing spent. So +2 is the reserve and +4 is the total spent. The comment on the PointWatch reader
    // said "spent JP" about +2 and was believed for months, because nothing ever contradicted it.
    unsigned short jpHeld[CS_JOB_N] = {};   // Job Points in reserve (the game caps this at 500)
    unsigned short jpSpent[CS_JOB_N] = {};  // Job Points spent over the life of the job

    int  attr_total(int a) const {       // base + gear, which is the number the status menu shows
        const int t = (int)base[a] + (int)added[a];
        return t < 0 ? 0 : t;
    }
    bool has_job(int jobId) const { return jobId > 0 && jobId < CS_JOB_N && jobLvl[jobId] > 0; }
};

// ---- the three decoders ---------------------------------------------------------------------------
// `bytes` is the packet's DECLARED length (party_state_internal.h : pkt_bytes). Every read is floored on the
// highest byte it touches, and a packet that cannot carry a block leaves that block's `ok` flag false instead
// of filling it with whatever followed in the buffer.

inline unsigned cs_u16(const unsigned char* p, int o) { return (unsigned)p[o] | ((unsigned)p[o + 1] << 8); }
inline short    cs_s16(const unsigned char* p, int o) { return (short)(unsigned short)cs_u16(p, o); }

// 0x061 : Maximum HP @04, MP @08, jobs @0C..0F, EXP @10/@12, base attributes @14..@20 (7 x u16), what the gear
// adds @22..@2E (7 x s16 -- signed), Attack @30, Defense @32, the eight resistances @34..@42, Title @44,
// Nation rank @46, Rank points @48.
inline bool cs_read_061(const unsigned char* p, int bytes, CharSheet& cs) {
    if (!p || bytes < 0x4A) return false;          // the highest field read is Rank points @0x48..0x49
    cs.maxHp = (unsigned)p[0x04] | ((unsigned)p[0x05] << 8) | ((unsigned)p[0x06] << 16) | ((unsigned)p[0x07] << 24);
    cs.maxMp = (unsigned)p[0x08] | ((unsigned)p[0x09] << 8) | ((unsigned)p[0x0A] << 16) | ((unsigned)p[0x0B] << 24);
    cs.mjob = p[0x0C]; cs.mlvl = p[0x0D]; cs.sjob = p[0x0E]; cs.slvl = p[0x0F];
    cs.xpCur = (unsigned short)cs_u16(p, 0x10);
    cs.xpNext = (unsigned short)cs_u16(p, 0x12);
    for (int a = 0; a < CS_ATTR_N; ++a) {
        cs.base[a]  = (unsigned short)cs_u16(p, 0x14 + a * 2);
        cs.added[a] = cs_s16(p, 0x22 + a * 2);
    }
    cs.attack  = (unsigned short)cs_u16(p, 0x30);
    cs.defense = (unsigned short)cs_u16(p, 0x32);
    for (int e = 0; e < CS_ELEM_N; ++e) cs.resist[e] = cs_s16(p, 0x34 + e * 2);
    cs.title      = (unsigned short)cs_u16(p, 0x44);
    cs.nationRank = (unsigned short)cs_u16(p, 0x46);
    cs.rankPoints = (unsigned short)cs_u16(p, 0x48);
    cs.statsOk = true;
    return true;
}

// 0x01B : Sub/Job unlock flags @0x0C (u32), the level of each job @0x49 (22 bytes, job id 1..22), the mastered
// flag of each job @0x68 (22 BITS), and the Master Level of each job @0x6D (22 bytes).
//
// THE MASTERY FLAGS ARE BITS, NOT BYTES. This read them as 22 bytes for months and agreed with Windower on only
// four of the eleven mastered jobs -- close enough to look right on a spot check, wrong on most of them. The
// packet's own layout (Windower fields.lua, `fields.incoming[0x01B]`) is:
//     bit[1] _junk1 ; job_master x 0x16 (one BOOLBIT each) ; bit[1] _junk2   -> 24 bits, 0x68..0x6A
//     unsigned short _junk3                                                 -> 0x6B
//     job_master_level x 0x16 (one BYTE each)                               -> 0x6D..0x82
// So the flag of job id `j` is bit `j` of the bitfield that starts at 0x68 (bit 0 is the padding bit), and the
// MASTER LEVELS were in this packet all along -- never read, while the same numbers were being hunted in memory.
//
// THE FLOOR IS ITS OWN, deliberately, and now there are three. The encumbrance bitfield this packet was already
// read for sits at 0x60 and floors at 0x64 : raising THAT floor to cover the job tables would throw away a short
// 0x01B that carries a perfectly good encumbrance field. The same argument splits the tables from each other --
// a packet long enough for the levels but not for the master levels must still give up the levels.
// (The Limbus floor raised one field too far cost a silent out-of-bounds read on 2026-09-15 ; the shape that
// avoids it is one floor per block, never one floor for the packet.)
inline bool cs_read_01b(const unsigned char* p, int bytes, CharSheet& cs) {
    if (!p || bytes < 0x5F) return false;          // levels @0x49..0x5E : the smallest block worth anything
    cs.unlockFlags = (unsigned)p[0x0C] | ((unsigned)p[0x0D] << 8) | ((unsigned)p[0x0E] << 16) | ((unsigned)p[0x0F] << 24);
    for (int j = 1; j < CS_JOB_N; ++j) cs.jobLvl[j] = p[0x49 + (j - 1)];
    cs.jobsOk = true;
    if (bytes >= 0x6B) {                           // the 24-bit mastery field, 0x68..0x6A
        for (int j = 1; j < CS_JOB_N; ++j)
            cs.mastered[j] = (unsigned char)((p[0x68 + (j >> 3)] >> (j & 7)) & 1);
        cs.masteredOk = true;                      // this block, and only this block, is where the flag is a measurement
    }
    if (bytes >= 0x83)                             // the master levels, 0x6D..0x82
        for (int j = 1; j < CS_JOB_N; ++j) cs.masterLvl[j] = p[0x6D + (j - 1)];
    return true;
}

// 0x063 order 5 : job_point_info[jobId] @0x0C, six bytes each, indexed by JOB ID (not id-1) -- Capacity Points
// u16 @+0, Job Points SPENT u16 @+2. The existing reader takes the main job's entry only ; this takes every job
// the packet is long enough to hold, and stops there rather than reading past the end.
inline bool cs_read_063_jobpoints(const unsigned char* p, int bytes, CharSheet& cs) {
    if (!p || bytes < 0x06) return false;
    if (cs_u16(p, 0x04) != 5) return false;        // another order : not ours, and not a malformed packet either
    bool any = false;
    for (int j = 1; j < CS_JOB_N; ++j) {
        const int e = 0x0C + j * 6;
        if (bytes < e + 6) break;                  // the table stops here : keep what was read, claim nothing more
        cs.cp[j]      = (unsigned short)cs_u16(p, e);
        cs.jpHeld[j]  = (unsigned short)cs_u16(p, e + 2);
        cs.jpSpent[j] = (unsigned short)cs_u16(p, e + 4);
        any = true;
    }
    if (any) cs.pointsOk = true;
    return any;
}

} // namespace aio
