// party_state.h -- live party data, fed by the inbound party packets (0x0DD / 0x0DF).
//
// Reversed from the packet hook (b = decoded packet). 0x0DD = full member update
// (incl. name + jobs), 0x0DF = frequent vitals update (HP/MP/TP). The HUD's Party
// widget reads this when it holds members, else it falls back to demo data.
#pragma once

namespace aio {

struct PMember {
    unsigned id = 0;
    int  hp = 0, mp = 0, tp = 0;     // current values
    int  hpp = 0, mpp = 0;           // % (0..100)  -> bar fill
    int  maxHp = 0, maxMp = 0;       // derived (so 0x0DF vitals can refresh the % too)
    int  mjob = 0, sjob = 0;         // job ids (0 = none)
    int  zone = 0;                   // 0x0DD @+0x20 : member's zone id when OUT of our zone (0 = in zone)
    unsigned flags = 0;              // 0x0DD flags @+0x14 (leadership bits ; tentative)
    float dist = -1.0f;              // horizontal distance to the player (yalms) ; -1 = unknown (out of zone / trust)
    char name[20] = {0};

    // NB: party/alliance leadership is now resolved by server-id vs allianceinfo_t (see
    // game_mem read_party_leaders) -- these two flag methods are superseded and unused for
    // display, kept only so old call sites compile. Only the QUARTERMASTER lives in the flag
    // mask: bit 0x10 (verified in-game 2026-06-26 -- the bit appeared on the member given QM).
    bool party_lead()    const { return false; }
    bool alliance_lead() const { return false; }
    bool quarter_master()const { return (flags & 0x10) != 0; }
};

// Cast state lives OUTSIDE PMember : the member array is rebuilt from memory every frame
// (load_from_memory -> pm = PMember()), which would wipe a packet-set cast. Keyed by server id,
// sized for a full alliance so any visible member's cast survives the per-frame roster refresh.
struct CastSlot { unsigned id = 0; unsigned spell = 0; unsigned startMs = 0; unsigned durMs = 0; };

// Member buffs from the party-buffs packet 0x076 (which never carries the local player -> self
// buffs come from memory). Keyed by server id, sized for a full alliance, refreshed each 0x076.
// Transient (NOT part of the cached roster). ids are FFXI status ids (uint16) ; n = count.
struct BuffSet { unsigned id = 0; int n = 0; unsigned short ids[32] = {}; };

struct PartyState {
    PMember m[6];
    int count = 0;
    CastSlot casts_[18];
    BuffSet  buffs_[18];

    // Live alliance roster (parties 2 & 3 = member-array slots 6..11 and 12..17), filled by
    // load_from_memory each frame. alli_[0..5] = alliance party 2, alli_[6..11] = party 3.
    PMember alli_[12];
    int      alliN_[2] = { 0, 0 };        // live member count for alliance party 2 / 3

    void on_dd(const unsigned char* p);   // 0x0DD : member update (name/jobs/HP/MP/TP/%) -> also caches
    void on_df(const unsigned char* p);   // 0x0DF : vitals update (HP/MP/TP, refresh %)
    void on_action(const unsigned char* p); // 0x028 : begin/finish casting -> drives the cast bar
    void on_076(const unsigned char* p);  // 0x076 : party-member status icons (buffs) -> buffs_[]
    const BuffSet* buffs_for(unsigned id) const;   // a member's buffs (null if none cached)
    int  alliance_count(int tier) const;           // live member count for alliance box tier 1 / 2
    const PMember& alliance_member(int tier, int i) const;   // member i of alliance box tier 1 / 2
    int  find(unsigned id) const;
    // current cast for member `id` : returns the spell name (or 0 if not casting / expired) and
    // fills `pctOut` with the 0..1 cast progress. Used by the party UI's cast line/bar.
    const char* cast_label(unsigned id, float& pctOut, float& alphaOut) const;

    void save() const;                    // persist the roster (rare ; on each 0x0DD)
    void load();                          // restore the cached roster at startup (instant party)
    void load_from_memory();              // seed the LIVE roster+vitals from FFXI memory (instant, accurate)
};

PartyState& party();                      // global live party
const char* job_abbr(int id);             // job id -> "WAR".."RUN" ("" if unknown)
int         job_id_from_abbr(const char* a);   // "WAR" -> 1 .. "RUN" -> 22 (0 if unknown) ; job-icon atlas cell = id-1
unsigned    job_role_color(int id);       // role tint (healer/buffer/tank/dd) ARGB -- icon + border + text

// DEMO mode (driven by //aio party|alliance1|alliance2 demo). 0 = off (live), 1 = party only,
// 2 = party + 1 alliance, 3 = party + 2 alliances. Each PartyList box shows its tier when
// level > tier (tier 0 = main party, 1 = alliance1, 2 = alliance2).
int  party_demo_level();
void set_party_demo_level(int level);
// party demo member count (1..6) -> //aio party demo N. Lets the demo preview the adaptive party
// height / mask / Set-Ref growth at any count (alliances always preview full 6).
int  party_demo_count();
void set_party_demo_count(int c);
// demo "selection" : when on, the demo boxes show a target cursor cycling through all members
// (party then alliances) so the config preview demonstrates the selection highlight. Off in real play.
bool demo_select();
void set_demo_select(bool on);
// TEST helper : append N fake members to the LIVE party (0..5) -> //aio sim N. Lets you watch the box
// grow + the alliances react to the main-party size without needing real players. 0 = off (real size).
int  party_sim_extra();
void set_party_sim_extra(int n);

} // namespace aio
