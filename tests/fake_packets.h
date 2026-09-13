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
struct Target { unsigned id; unsigned message; };
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
        put_bits(p, off + 44, 10, t.message);
        off += 86;                              // add-effect flag left 0
        off += 1;                               // spike flag 0
    }
    return p;
}
// A spell YOU finish casting (category 4). 236 = "the effect landed" ; 75 = "no effect".
inline Packet pkt_cast(unsigned actor, unsigned spell, std::initializer_list<Target> targets) { return pkt_action(actor, 4, spell, targets); }

inline void deliver(const Packet& p) { packet(p.id, p.b); }

} // namespace fake
