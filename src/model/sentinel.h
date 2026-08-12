// sentinel.h -- cross-check the two independent sources AioHUD already receives, and shout when they stop
// agreeing.
//
// WHY. ffximain_rva.cpp repairs a MOVE : an address that slid, which fails loudly (a feature goes dark) and
// can be re-derived. This file is about the other class, the one nothing can repair automatically and
// nobody notices : a field that shifts inside a STRUCT or inside a PACKET. Those do not go dark. They keep
// returning a number, the number is plausible, and it is wrong. You find out three weeks later and cannot
// say since when.
//
// The leverage is that several values reach us TWICE, by paths that break independently : the server sends
// them in a packet, and the client also keeps them in memory. Today we pick one and ignore the other. Made
// to disagree out loud, that redundancy becomes an alarm for breakage nobody predicted -- including the kind
// no amount of re-pinning would have caught.
//
// THIS FILE NEVER REPAIRS ANYTHING. A cross-check cannot tell WHICH side is wrong, so acting on it would be
// guessing with the user's data. It reports, names both values, and stops there.
//
// Choosing pairs is the whole design, and the trap is obvious in hindsight : compare HP and you compare two
// samples taken at different instants, so it disagrees constantly and the alarm becomes noise nobody reads.
// Every pair below compares something STABLE (a name, a job, a level) or a set OVERLAP that a single
// expiring entry cannot break. An alarm that cries wolf once is an alarm that is off forever.
#pragma once

namespace aio {

struct GameState;

enum SentinelPair {
    SEN_MEMBER  = 0,   // packet 0x0DD name/job/level  vs  the party member block in memory
    SEN_BUFFS,         // packet 0x063 order 9 buff ids  vs  the self buff array in memory
    SEN_POINTWATCH,    // packet 0x061/0x063 values  vs  the FFXiMain static block (reported by the re-pinner)
    SEN_N
};

// --- the packet side, handed over from the handlers that already decode it (no second parse) ---
void sentinel_packet_member(unsigned id, const char* name, unsigned mjob, unsigned mlvl);
void sentinel_packet_buffs(const unsigned short* ids, int n);

// The re-pinner calls this when a packet's values match NOWHERE in the image : that is not an address that
// moved, it is a layout that changed, and it belongs here rather than in a log line nobody re-reads.
void sentinel_note_unmatched(SentinelPair p, const char* detail);

// Once per frame, at the end of the poll : runs whichever comparison has come due.
void sentinel_tick(const GameState& gs);

bool        sentinel_diverged(SentinelPair p);
const char* sentinel_report_name(SentinelPair p);   // the pair's label, for a user-facing message
int  sentinel_report(char out[][160], int maxOut);   // one line per pair, for //aio doctor

} // namespace aio
