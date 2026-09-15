// party_state_pointwatch.cpp -- PointWatch module (XP / CP / ML + Merits progression),
// split out of party_state.cpp. PURE MOVE : the three packet handlers that feed PointWatch --
// on_char_stats (0x061), on_set_update (0x063 Order 2/5/9), on_exp_msg (0x029/0x02D live gains).
// The RateReg X/h ring + ffxi_now_tick stay in party_state.cpp (shared / used by other modules).
#include "model/model_clock.h"   // model_now_ms / model_now_unix : one frozen clock per model event
#include "model/party_state.h"
#include "model/party_state_internal.h"   // pkt_u16 / pkt_u32 (shared packet readers)
#include "model/ffximain_rva.h"           // fm_pw_expect : these packets are what re-pins the static block
#include "model/sentinel.h"                // the same packets are the ground truth the cross-check compares against
#include "windower_debug.h"   // //aio songtape : the self-status diff is logged from here

namespace aio {

void PartyState::on_char_stats(const unsigned char* p) {   // 0x061 Char Stats
    if (pkt_short(p, 0x70)) return;                        // truncated -> don't read garbage (reads the EP-to-next u32 @0x6C)
    pwMainJob_    = p[0x0C];
    pw_.jobLevel  = p[0x0D];
    pw_.xpCur     = pkt_u16(p, 0x10);
    pw_.xpTnl     = pkt_u16(p, 0x12);
    pw_.masterLevel = p[0x65];
    pw_.epCur     = pkt_u32(p, 0x68);
    pw_.epTnml    = pkt_u32(p, 0x6C);
    pw_.valid     = true;
    // The client mirrors this very body into a FFXiMain static block. Hand the values over : they are the
    // ground truth that lets that block be FOUND AGAIN after a client patch moves it, with nothing asked
    // of the player. (Checked a few frames later -- our hook runs before the client writes its own copy.)
    fm_pw_expect(pw_.xpCur, pw_.xpTnl, pw_.masterLevel, pw_.epCur, pw_.epTnml);
}
void PartyState::on_set_update(const unsigned char* p) {   // 0x063 Set Update
    if (pkt_short(p, 0x06)) return;                        // need the order field @0x04 ; each order then floors on its OWN fields (their sizes differ wildly -- 0x0D vs 0xC8 -- so a single top floor would wrongly drop the small orders)
    const unsigned order = pkt_u16(p, 0x04);
    if (order == 2) {                                       // Order 2 : merits
        if (pkt_short(p, 0x0D)) return;                    // reads up to maxMerits @0x0C
        pw_.lpCur     = pkt_u16(p, 0x08);
        pw_.merits    = p[0x0A] & 0x7F;                     // bit[7]
        pw_.maxMerits = p[0x0C];
        fm_pw_merit_expect(pw_.lpCur, pw_.merits, pw_.maxMerits);   // ground truth for the merit block's address
    } else if (order == 5 && pwMainJob_ >= 1 && pwMainJob_ <= 23) {   // Order 5 : per-job Capacity / Job Points
        const int e = 0x0C + (int)pwMainJob_ * 6;           // job_point_info[jobId] (6 bytes each)
        if (pkt_short(p, e + 4)) return;                     // reads the u16 @e and @e+2 (up to e+3 ; e grows with the job id)
        pw_.cpCur = pkt_u16(p, e);
        pw_.cpJp  = (int)pkt_u16(p, e + 2);
    } else if (order == 9) {                                // Order 9 : SELF BUFF TIMERS -- Buffs u16[32] @0x08, expiry
        if (pkt_short(p, 0xC8)) return;                    // reads Time u32[31] up to p[0xC7] -> a runt would clear then refill from garbage
        // THE TAPE DIFFS THIS LIST rather than dumping it. What matters is the INSTANT a status appears or
        // disappears on you: that is the game stating, authoritatively, that a song landed or was replaced --
        // the observation the ally-buff model currently replaces with an inference over three lists that
        // arrive at different times.
        const bool tape = song_tape_on();
        unsigned short wasIds[32]; int wasN = 0;
        if (tape) { wasN = buffTimerN_ < 32 ? buffTimerN_ : 32; for (int i = 0; i < wasN; ++i) wasIds[i] = buffTimers_[i].id; }
        buffTimerN_ = 0; buffTimersMs_ = model_now_ms();                                    //   Time u32[32] @0x48 (absolute FFXI 1/60s ticks). Full refresh.
        for (int i = 0; i < 32; ++i) {
            const unsigned bid = pkt_u16(p, 0x08 + i * 2);
            if (bid == 0xFFFF || bid == 0xFF || bid == 0) continue;   // empty slot
            note_status_seen(bid);   // same registry as the 0x076 path : the config lists statuses that really occur
            buffTimers_[buffTimerN_].id = (unsigned short)bid;
            buffTimers_[buffTimerN_].expiry = pkt_u32(p, 0x48 + i * 4);
            ++buffTimerN_;
        }
        if (tape) {
            // Counted, not just present/absent: two songs can share one status (two Marches), so "198 x2 -> x1"
            // is the fact, and a plain set difference would show nothing at all.
            for (int pass = 0; pass < 2; ++pass) {
                const unsigned short* a = pass ? wasIds : 0;
                for (int i = 0; i < (pass ? wasN : buffTimerN_); ++i) {
                    const unsigned short id = pass ? a[i] : buffTimers_[i].id;
                    bool dup = false;
                    for (int q = 0; q < i; ++q) if ((pass ? a[q] : buffTimers_[q].id) == id) { dup = true; break; }
                    if (dup) continue;
                    int nowC = 0, wasC = 0;
                    for (int q = 0; q < buffTimerN_; ++q) if (buffTimers_[q].id == id) ++nowC;
                    for (int q = 0; q < wasN; ++q)        if (wasIds[q] == id)          ++wasC;
                    if (nowC != wasC && (pass ? nowC == 0 : true))
                        windower::debug::log("TAPE %6u  SELF   status %-4u  %d -> %d   (your own 0x063 : the server's own word)",
                                             tape_ms(), (unsigned)id, wasC, nowC);
                }
            }
        }
        latch_co_expiry_casters();
        // The server just named every buff you are carrying. Memory holds the same list by an entirely
        // different path, so handing this over turns the redundancy into an alarm for the day one of the
        // two silently stops meaning what it used to.
        { unsigned short ids[32]; for (int i = 0; i < buffTimerN_ && i < 32; ++i) ids[i] = buffTimers_[i].id;
          sentinel_packet_buffs(ids, buffTimerN_); }
    }
}
// A trust move can grant SEVERAL statuses while the 0x028 names only ONE (MEASURED : Monberaux's move gives Protect
// AND Shell, the packet reports status 40 only). The co-expiring partner is identifiable while both are fresh, so
// LATCH the attribution into buffCaster_ here instead of re-deriving it every frame -- the anchor is temporary.
// Ygnas later recast Protectra V over the Protect, which replaced the anchor's expiry and orphaned Shell for the
// rest of its life. Latching at refresh time keeps what we learned while it was still learnable.
void PartyState::latch_co_expiry_casters() {
    for (int i = 0; i < buffTimerN_; ++i) {
        const unsigned short st = buffTimers_[i].id;
        if (st >= 1024) continue;
        if (buffCaster_[st] && caster_resolves(buffCaster_[st])) continue;   // already attributed to a LIVE caster -> leave it
        buffCaster_[st] = 0;                                                // stale id (trusts re-summoned) -> forget it and re-derive
        unsigned found = 0; bool split = false;
        for (int j = 0; j < buffTimerN_; ++j) {
            if (j == i || buffTimers_[j].expiry != buffTimers_[i].expiry) continue;
            const unsigned short o = buffTimers_[j].id;
            const unsigned c = (o < 1024) ? buffCaster_[o] : 0;
            if (!c || c == selfId_ || !caster_resolves(c)) continue;   // NEVER latch from a SELF-attributed partner. Your own buffs are already
                                                                       // attributed directly (the isMe path) ; borrowing self here mis-claimed a
                                                                       // trust's UNNAMED mix-boost (STR/DEX/AGI/INT/CHR) that merely shared an
                                                                       // expiry tick with your own live Gain-MND -> it stuck to you permanently.
            if (found && found != c) { split = true; break; }  // co-expiring statuses disagree -> don't guess
            found = c;
        }
        if (found && !split) buffCaster_[st] = found;
    }
}
void PartyState::on_exp_msg(const unsigned char* p, unsigned id) {   // 0x029 (Param1 @0x0C) / 0x02D (Param1 @0x10)
    if (pkt_short(p, 0x1A)) return;                        // reads the msg id u16 @0x18 (the val @0x0C/0x10 sits below it)
    const unsigned val = pkt_u32(p, id == 0x029 ? 0x0C : 0x10);
    const unsigned msg = pkt_u16(p, 0x18);
    if (!val) return;
    if (msg == 8 || msg == 105) {                           // XP gained
        pw_.xpReg.add((int)val);
        pw_.xpCur += val; if (pw_.xpCur > 55999u) pw_.xpCur = 55999u;
        if (pw_.xpTnl && pw_.xpCur > pw_.xpTnl) pw_.xpCur -= pw_.xpTnl;
    } else if (msg == 718 || msg == 735) {                  // Capacity Points gained
        pw_.cpReg.add((int)val);
        pw_.cpCur += val;
        if (pw_.cpCur > 30000u && pw_.cpJp != 500) { pw_.cpJp += (int)(pw_.cpCur / 30000u); if (pw_.cpJp > 500) pw_.cpJp = 500; pw_.cpCur %= 30000u; }
    } else if (msg == 371 || msg == 372) {                  // Limit Points gained (merits)
        pw_.lpCur += val;
        if (pw_.lpCur >= 10000u && pw_.merits != pw_.maxMerits) { pw_.merits += (int)(pw_.lpCur / 10000u); if (pw_.maxMerits && pw_.merits > pw_.maxMerits) pw_.merits = pw_.maxMerits; pw_.lpCur %= 10000u; }
        else if (pw_.lpCur > 9999u) pw_.lpCur = 9999u;
    } else if (msg == 809 || msg == 810) {                  // Exemplar Points gained (Master Level)
        pw_.epReg.add((int)val);
        pw_.epCur += val;
        if (pw_.epTnml && pw_.epCur >= pw_.epTnml) pw_.epCur -= pw_.epTnml;
    }
}

} // namespace aio
