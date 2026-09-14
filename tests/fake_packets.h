// fake_packets.h -- incoming FFXI packets, built in the layout the MODEL's parsers read.
//
// The layouts below are the model's own (party_state.cpp : on_action / action_target_ids / on_076 ;
// party_state_pointwatch.cpp : on_set_update order 9), not a second opinion from a wiki. What makes them trustworthy
// is not this comment but tests/t_timers.cpp's first section, which feeds each builder to the real handler and
// checks the model recorded what was meant -- a builder that drifted from the parser fails there, by name, before
// any Timers scenario can pass on garbage.
#pragma once
#include "fake_game.h"
#include <cstring>
#include <initializer_list>

namespace fake {

struct Packet { unsigned char b[512]; int id; };

inline void pkt_header(Packet& p, int id, int bytes) {
    memset(p.b, 0, sizeof(p.b)); p.id = id;
    const int words = (bytes + 3) / 4;
    const unsigned hdr = (unsigned)id | ((unsigned)words << 9);   // u16@0 = id (9 bits) | size in dwords (7 bits)
    p.b[0] = (unsigned char)(hdr & 0xFF); p.b[1] = (unsigned char)(hdr >> 8);
}
inline void put_u16(Packet& p, int o, unsigned v) { p.b[o] = (unsigned char)v; p.b[o + 1] = (unsigned char)(v >> 8); }
inline void put_u32(Packet& p, int o, unsigned v) { for (int i = 0; i < 4; ++i) p.b[o + i] = (unsigned char)(v >> (8 * i)); }
// LSB-first bit writer, the inverse of party_state.cpp getbits().
inline void put_bits(Packet& p, int bitoff, int width, unsigned v) {
    for (int i = 0; i < width; ++i) {
        const int bit = bitoff + i;
        if (v & (1u << i)) p.b[bit >> 3] |= (unsigned char)(1u << (bit & 7));
        else               p.b[bit >> 3] &= (unsigned char)~(1u << (bit & 7));
    }
}

// ---- 0x063 order 9 : YOUR buff timers. Buffs u16[32] @0x08, expiry u32[32] @0x48 (absolute FFXI ticks, 1/60 s). ----
struct Timer { unsigned short status; int secondsLeft; };
inline Packet pkt_self_timers(std::initializer_list<Timer> timers) {
    Packet p; pkt_header(p, 0x063, 0xC8);
    put_u16(p, 0x04, 9);
    int i = 0;
    for (int k = 0; k < 32; ++k) put_u16(p, 0x08 + k * 2, 0xFFFF);   // empty slots
    for (const Timer& t : timers) {
        if (i >= 32) break;
        put_u16(p, 0x08 + i * 2, t.status);
        put_u32(p, 0x48 + i * 4, now_tick() + (unsigned)(t.secondsLeft * 60));
        ++i;
    }
    return p;
}

// ---- 0x076 : party members' status icons. 5 slots x 48 bytes : id u32 @+4, high bits @+12 (2 bits per status), low
//      byte @+20 (32 statuses, 0xFF = empty). ----
struct MemberBuffs { unsigned id; std::initializer_list<unsigned short> statuses; };
inline Packet pkt_party_buffs(std::initializer_list<MemberBuffs> members) {
    Packet p; pkt_header(p, 0x076, 0xF4);
    int k = 0;
    for (const MemberBuffs& m : members) {
        if (k >= 5) break;
        const int base = k * 48;
        put_u32(p, base + 4, m.id);
        for (int i = 0; i < 32; ++i) p.b[base + 20 + i] = 0xFF;   // empty (255 with hi bits 0)
        int i = 0;
        for (unsigned short s : m.statuses) {
            if (i >= 32) break;
            p.b[base + 20 + i] = (unsigned char)(s & 0xFF);
            p.b[base + 12 + (i >> 2)] |= (unsigned char)(((s >> 8) & 3) << (2 * (i & 3)));
            ++i;
        }
        ++k;
    }
    return p;
}

// ---- 0x028 : an action. Actor @bit 40 (32), target count @72 (6), category @82 (4), param @86 (16) ; target blocks
//      from bit 150 : id (32) + action count (4), then per action 86 bits -- param @+27 (17), message @+44 (10),
//      add-effect flag @+85 -- and a 1-bit spike flag. No add-effect, no spike : 123 bits a target. ----
struct Target { unsigned id; unsigned message; unsigned param = 0; };   // param : the result's value -- the STATUS for a "gains the effect" message (230)
inline Packet pkt_action(unsigned actor, unsigned category, unsigned param, std::initializer_list<Target> targets) {
    Packet p;
    const int nt = (int)targets.size();
    const int bits = 150 + nt * 123;
    pkt_header(p, 0x028, (bits + 7) / 8 + 8);
    put_bits(p, 40, 32, actor);
    put_bits(p, 72, 6, (unsigned)nt);
    put_bits(p, 82, 4, category);
    put_bits(p, 86, 16, param);
    int off = 150;
    for (const Target& t : targets) {
        put_bits(p, off, 32, t.id);
        put_bits(p, off + 32, 4, 1);          // one action on this target
        off += 36;
        put_bits(p, off + 27, 17, t.param);
        put_bits(p, off + 44, 10, t.message);
        off += 86;                              // add-effect flag left 0
        off += 1;                               // spike flag 0
    }
    return p;
}
// A spell YOU finish casting (category 4). 236 = "the effect landed" ; 75 = "no effect".
inline Packet pkt_cast(unsigned actor, unsigned spell, std::initializer_list<Target> targets) { return pkt_action(actor, 4, spell, targets); }

// ---- 0x0DD : a party member update. id u32 @0x04, HP @0x08, MP @0x0C, TP @0x10, HP% @0x1D, MP% @0x1E, zone u16 @0x20,
//      main job @0x22, main level @0x23, sub job @0x24, sub level @0x25, name @0x28. The two percentages are laid out as
//      MEASURED on real packets (2026-09-13 : byte 0x1D = 91 with 2243/2464 HP, byte 0x1E = 76 with 412/542 MP), which
//      is also Windower's layout -- NOT the model's own parser, which read them swapped. ----
//      SIZE : 52 bytes (0x34) declared, measured on two real packets (Tetsouo, Kaories) -- the name field ends at 0x33.
//      The model's parser demanded 0x3B and so rejected EVERY real 0x0DD since the first commit.
struct MemberUpdate { unsigned id; const char* name; unsigned hp, mp, tp, hpp, mpp, mjob, mlvl, sjob, slvl, zone; };
inline Packet pkt_member_update(const MemberUpdate& m) {
    Packet p; pkt_header(p, 0x0DD, 0x34);
    put_u32(p, 0x04, m.id); put_u32(p, 0x08, m.hp); put_u32(p, 0x0C, m.mp); put_u32(p, 0x10, m.tp);
    p.b[0x1D] = (unsigned char)m.hpp; p.b[0x1E] = (unsigned char)m.mpp;
    put_u16(p, 0x20, m.zone);
    p.b[0x22] = (unsigned char)m.mjob; p.b[0x23] = (unsigned char)m.mlvl; p.b[0x24] = (unsigned char)m.sjob; p.b[0x25] = (unsigned char)m.slvl;
    for (int i = 0; m.name && m.name[i] && i < 11; ++i) p.b[0x28 + i] = (unsigned char)m.name[i];   // 12 bytes, NUL-terminated
    return p;
}

// ---- 0x0D2 : an item enters (or leaves) the treasure pool. item u16 @0x10, slot @0x14, drop time (unix) u32 @0x18. ----
inline Packet pkt_pool_item(int slot, unsigned item, unsigned dropUnix) {
    Packet p; pkt_header(p, 0x0D2, 0x20);
    put_u16(p, 0x10, item); p.b[0x14] = (unsigned char)slot; put_u32(p, 0x18, dropUnix);
    return p;
}
// ---- 0x0D3 : lot info for a slot. highest lot u16 @0x0E, slot @0x14, drop flag @0x15 (!=0 : won or floored, gone),
//      highest lotter name @0x16 (16 chars). ----
inline Packet pkt_pool_lot(int slot, unsigned lot, const char* lotter, bool gone) {
    Packet p; pkt_header(p, 0x0D3, 0x28);
    put_u16(p, 0x0E, lot); p.b[0x14] = (unsigned char)slot; p.b[0x15] = gone ? 1 : 0;
    for (int i = 0; lotter && lotter[i] && i < 15; ++i) p.b[0x16 + i] = (unsigned char)lotter[i];
    return p;
}

// ================= the zone tracker, PointWatch gains, job info and pets (tests/t_packets.cpp) =================
// Same contract as above : each layout is the one the model's handler reads (party_state_zonetracker.cpp,
// party_state_pointwatch.cpp, party_state.cpp on_01b, party_state_hate.cpp), and t_packets.cpp's first section
// feeds every builder to its handler before any scenario relies on it.

// Shrinks the DECLARED size (header dwords) and nothing else : the bytes past it stay in the buffer, as they do in the
// client's decode buffer for a truncated packet. A handler that ignores its size floor then reads them and is caught.
inline void pkt_truncate(Packet& p, int bytes) {
    const unsigned hdr = (unsigned)p.id | ((unsigned)((bytes + 3) / 4) << 9);
    p.b[0] = (unsigned char)(hdr & 0xFF); p.b[1] = (unsigned char)(hdr >> 8);
}

// ---- 0x02A : a zone message. Params p1..p4 u32 @0x08/0x0C/0x10/0x14, target entity INDEX u16 @0x18, message id
//      u16 @0x1A (the handler masks it : 0x3FFF Abyssea/Limbus, 0x7FFF Sheol). 32 bytes. ----
inline Packet pkt_zone_msg(unsigned msg, int p1, int p2 = 0, int p3 = 0, int p4 = 0, unsigned targetIndex = 0) {
    Packet p; pkt_header(p, 0x02A, 0x20);
    put_u32(p, 0x08, (unsigned)p1); put_u32(p, 0x0C, (unsigned)p2); put_u32(p, 0x10, (unsigned)p3); put_u32(p, 0x14, (unsigned)p4);
    put_u16(p, 0x18, targetIndex); put_u16(p, 0x1A, msg);
    return p;
}

// ---- 0x055 : one key-item TABLE. Owned bits u8[0x40] @0x04 (bit = id - table*512), table number u32 @0x84. The
//      Dynamis granules 1545..1549 are table 3, bits 9..13 ; the Nyzul armband 797 is table 1, bit 285. 136 bytes. ----
inline Packet pkt_key_items(unsigned table, std::initializer_list<unsigned> ids) {
    Packet p; pkt_header(p, 0x055, 0x88);
    for (unsigned id : ids) {
        if (id / 512 != table) continue;
        const unsigned bit = id % 512;
        p.b[0x04 + bit / 8] |= (unsigned char)(1u << (bit % 8));
    }
    put_u32(p, 0x84, table);
    return p;
}

// ---- 0x118 : currency2. Mog Segments u32 @0x8C, Temenos Units u32 @0x98, Apollyon Units u32 @0x9C. 160 bytes. ----
inline Packet pkt_currency2(unsigned mogSegments, unsigned temenosUnits, unsigned apollyonUnits) {
    Packet p; pkt_header(p, 0x118, 0xA0);
    put_u32(p, 0x8C, mogSegments); put_u32(p, 0x98, temenosUnits); put_u32(p, 0x9C, apollyonUnits);
    return p;
}

// ---- 0x034 : an NPC menu opens. Actor id u32 @0x04 (the handler compares it with YOUR id), menu parameter[0] u32
//      @0x08, menu id u16 @0x2C (173 = the Rabao Odyssey conflux). 52 bytes. ----
inline Packet pkt_npc_menu(unsigned actorId, unsigned menuId, unsigned param0) {
    Packet p; pkt_header(p, 0x034, 0x34);
    put_u32(p, 0x04, actorId); put_u32(p, 0x08, param0); put_u16(p, 0x2C, menuId);
    return p;
}

// ---- 0x075 : battlefield bars. Designation u32 @0x04, start u32 @0x08, duration/countdown u32 @0x0C ; six bars at
//      0x28 + i*0x14, each { s32 progress ; char label[16] }. An unused bar : progress 0x7FFFFFFF, empty label. ----
struct Bar { int progress; const char* label; };
inline Packet pkt_battlefield(std::initializer_list<Bar> bars, unsigned desig = 0, unsigned start = 0, unsigned seconds = 0) {
    Packet p; pkt_header(p, 0x075, 0xA0);
    put_u32(p, 0x04, desig); put_u32(p, 0x08, start); put_u32(p, 0x0C, seconds);
    for (int i = 0; i < 6; ++i) put_u32(p, 0x28 + i * 0x14, 0x7FFFFFFFu);
    int i = 0;
    for (const Bar& b : bars) {
        if (i >= 6) break;
        const int off = 0x28 + i * 0x14;
        put_u32(p, off, (unsigned)b.progress);
        for (int k = 0; b.label && b.label[k] && k < 16; ++k) p.b[off + 4 + k] = (unsigned char)b.label[k];   // 16 bytes, NOT terminated when full
        ++i;
    }
    return p;
}

// ---- 0x02D : a point gain message. Param1 (the gain) u32 @0x10, message id u16 @0x18. 28 bytes. (0x029 carries its
//      Param1 @0x0C instead ; the handler picks by id.) 8/105 XP, 718/735 CP, 371/372 Limit Points, 809/810 EP. ----
inline Packet pkt_exp_msg(unsigned msg, unsigned value, unsigned param2 = 0) {
    Packet p; pkt_header(p, 0x02D, 0x1C);
    put_u16(p, 0x0C, 0x0400);            // YOUR entity index sits here (the target's @0x0E) : NOT the gain
    put_u32(p, 0x10, value); put_u32(p, 0x14, param2); put_u16(p, 0x18, msg);
    return p;
}

// ---- 0x01B : job info. Encumbrance flags u32 @0x60 (bit sid = equip slot sid locked). 104 bytes. ----
inline Packet pkt_job_info(unsigned encumbrance) {
    Packet p; pkt_header(p, 0x01B, 0x68);
    put_u32(p, 0x60, encumbrance);
    return p;
}

// ---- 0x068 : pet status. Owner id u32 @0x08, pet entity INDEX u16 @0x0C, pet's target id u32 @0x14. 24 bytes is the
//      handler's floor ; the real packet goes on with the pet's HP/MP/TP and name. ----
inline Packet pkt_pet_status(unsigned ownerId, unsigned petIndex, unsigned targetId) {
    Packet p; pkt_header(p, 0x068, 0x30);
    put_u32(p, 0x08, ownerId); put_u16(p, 0x0C, petIndex); put_u32(p, 0x14, targetId);
    return p;
}

inline void deliver(const Packet& p) { packet(p.id, p.b); }

} // namespace fake
