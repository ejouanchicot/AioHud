// party_state_internal.h -- shared inline helpers used by party_state.cpp and its split module TUs. Not public.
#pragma once
#include "windower.h"
#include "model/party_state.h"   // HateEntry (for record_hate below)
namespace aio {
inline unsigned pkt_u16(const unsigned char* p, int o) { return (unsigned)p[o] | ((unsigned)p[o + 1] << 8); }
inline unsigned pkt_u32(const unsigned char* p, int o) { return (unsigned)p[o] | ((unsigned)p[o + 1] << 8) | ((unsigned)p[o + 2] << 16) | ((unsigned)p[o + 3] << 24); }
// Declared packet length in bytes (FFXI header : u16@0 = id(9 bits) | size-in-dwords(7 bits)). A truncated
// packet still lands in the decode buffer, so the SEH guard alone won't catch it reading garbage past the
// real end -> each handler floors on its highest field offset before parsing (0x029 already did).
inline int pkt_bytes(const unsigned char* p) {
    return (int)(((((unsigned)p[0] | ((unsigned)p[1] << 8)) >> 9) & 0x7F) * 4);
}
// ---- A REJECTED PACKET MUST BE COUNTABLE ------------------------------------------------------------------
// The 19 floors above are the protection ; being SILENT is the defect. A packet thrown away for being short
// looks, from every box it feeds, exactly like a packet the server never sent -- and that is the first
// question //aio doctor exists to answer. Rule 10's corollary: instrument the DECISION, and say when a thing
// is refused.
//
// ONLY THE LENGTH FLOOR AND A MALFORMED SECTION COUNT. A handler that returns because the message is not for
// it, the zone is wrong or the sender is a stranger is doing its JOB -- counting those would bury the signal
// under thousands of normal refusals and make the number worthless. That distinction is the whole contract of
// this helper: it is named for the floor, and nothing else may call it.
//
// The id is not passed at the call site : model_feed_packet stamps the one being dispatched (pkt_dispatch_id),
// so a handler cannot mis-attribute its own rejects by typing the wrong literal. Packets are fed inline on the
// game's main loop (measured in every capture -- see aiohud.cpp), so one current id is enough.
void pkt_note_reject(int cause, int need, int got);   // party_state.cpp
enum { PKTREJ_SHORT = 0, PKTREJ_SECTION = 1 };
// true = too short for `need` bytes, and counted. `if (pkt_short(p, 0x18)) return;` replaces the bare compare.
inline bool pkt_short(const unsigned char* p, int need) {
    const int got = pkt_bytes(p);
    if (got >= need) return false;
    pkt_note_reject(PKTREJ_SHORT, need, got);
    return true;
}
// A section INSIDE a packet that cannot be what it claims (a count that overruns the declared end, a variable
// block that does not fit). Same counter, separate cause -- they fail for different reasons and a capture
// wants to tell them apart.
inline void pkt_note_bad_section(int need, int got) { pkt_note_reject(PKTREJ_SECTION, need, got); }

// HATE LIST enmity tracker : record/refresh a mob->PC pair. Defined in party_state_hate.cpp ; called by
// on_pet_status (party_state_hate.cpp) AND the hate block in on_action (party_state.cpp).
void record_hate(HateEntry* h, unsigned mob, unsigned pc);
}
