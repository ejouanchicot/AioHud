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
    int  mlvl = 0;                   // main-job level (member +0x72 ; 0 = unknown)
    int  slvl = 0;                   // sub-job level (member +0x74 ; already the displayed/capped value ; 0 = unknown)
    int  zone = 0;                   // 0x0DD @+0x20 : member's zone id when OUT of our zone (0 = in zone)
    unsigned flags = 0;              // 0x0DD flags @+0x14 (leadership bits ; tentative)
    float dist = -1.0f;              // horizontal distance to the player (yalms) ; -1 = unknown (out of zone / trust)
    unsigned char isTrust = 0;       // 1 = a Trust NPC (name matched the TRUSTS DB ; packet carries no job for trusts)
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
struct CastSlot { unsigned id = 0; unsigned spell = 0; unsigned startMs = 0; unsigned durMs = 0; unsigned char kind = 0; };   // kind : 0 = spell (cat 8), 1 = weaponskill/ability/TP move (cat 7 "readies")

// YOUR last weaponskill -> the arcade "ULTRA COMBO" centre-screen popup (name + total damage). Set on the
// 0x028 category-3 (WS finish) from the local player ; the HUD animates + fades it over ~2.4s.
struct WsPopup { char name[40] = {0}; int dmg = 0; unsigned startMs = 0; };

// TREASURE POOL : one lottery slot (module : TreasurePool). Built from the drop packets 0x0D2 (item added :
// Item id / Index / Timestamp) + 0x0D3 (lot info : highest lotter name+lot / Drop=won). Indexed directly by
// the packet Index (0..9). itemId 0 = empty slot ; expires 5 min after the drop. See game-data/treasure-pool.md.
struct TreasureItem {
    unsigned short itemId = 0;    // 0 = empty slot
    unsigned timestamp = 0;       // packet drop Timestamp (unix) -> dup detection
    unsigned expireUnix = 0;      // unix time when it drops out of the pool (~5 min)
    unsigned short lot = 0;       // highest lot value (0 = nobody lotted)
    char lotter[20] = {0};        // highest lotter's name
};

// SKILLCHAINS : a resonating window on a target (module : Skillchains). Opened at step 1 by a WS/spell
// FINISH that carries skillchain properties, then CLOSED (step+1) when a following hit's 0x028 add-effect
// is a skillchain. All from the action packet (skillchain.h has the bit offsets + tables). Timing in abs
// GetTickCount ms : [openMs, delayMs) = "Wait", [delayMs, endMs) = "Go!/Burst" window, then it expires.
struct Resonating {
    unsigned target = 0;          // mob id (0 = free slot)
    unsigned actionId = 0;        // the opening/closing action id -> its name (ws_info/spell_info/mobskill)
    unsigned char resource = 0;   // SCResource of actionId (0 WS / 1 spell / 2 mob / 3 JA / 4 element)
    unsigned char prop[3] = {255,255,255};  // active properties (a lone WS = its opening props ; a close = the 1 skillchain)
    unsigned char nProp = 0;      // number of active properties
    unsigned char step = 0;       // chain length (1 = a lone WS opened a window)
    unsigned char lvl = 0;        // skillchain level (1..4)
    unsigned char closed = 0;     // lvl4 or step>5 -> burst only (no further continuation)
    unsigned char formed = 0;     // a real skillchain CLOSED on it (step>=2) -> show burst ELEMENTS, not just props
    unsigned openMs = 0, delayMs = 0, endMs = 0;
};

// HATE LIST : the mobs that have aggro on you / your party (module : HateList). Enmity is tracked from the
// 0x028 action packet -- a PC(or party pet)<->mob action pairs that mob to that PC (hate_[]). Once per frame the
// tracked mobs are resolved to display rows (hateRows_[], name/HP%/distance/claim) via one entity-array read, then
// pruned (dead / idle / >50 yalms / stale). Mirrors the reference addon's enemybar2 actionTracking. NOT snapshot :
// like the roster it lives in the party() singleton (the widget reads hate_rows()).
struct HateEntry { unsigned mob = 0;     // aggroing mob's server id (0 = free slot)
                   unsigned pc  = 0;     // the PC (party member) it's fixed on (0 = unknown)
                   unsigned lastMs = 0; };  // GetTickCount of the last action linking them -> staleness prune
struct HateRow  { unsigned id = 0; char mob[24] = {0}; char pc[20] = {0}; int hpp = 0; float dist = 0.0f;
                  unsigned char red = 0, target = 0; };   // red = claimed by someone ; target = it's your <t>

// Member buffs from the party-buffs packet 0x076 (which never carries the local player -> self
// buffs come from memory). Keyed by server id, sized for a full alliance, refreshed each 0x076.
// Transient (NOT part of the cached roster). ids are FFXI status ids (uint16) ; n = count.
struct BuffSet { unsigned id = 0; int n = 0; unsigned short ids[32] = {}; };

// Debuffs ON A TARGET (mob), inferred from the 0x028 action packet : FFXI does NOT store a
// readable per-mob status list (see docs target-substruct.md), so we TRACK the debuffs the local
// player lands (category-4 finish, param = status id) keyed by the CURRENT target id. No real
// remaining-time exists in the client -> ICONS ONLY (the packet's promising field was the animation,
// not a duration). Entries self-expire after an APPROX base duration per status (debuff_base_ms) so the
// icon clears roughly when the debuff wears -- Stun goes fast, DoTs linger. No countdown is ever shown.
// Per-target debuff tracking : one DebuffSet per live mob id. Sized for AoE content -- a -ga / Horde song can
// debuff a whole same-name pack in one cast, and each mob needs its OWN slot ; too few slots made packs collapse
// into slot 0, so two mobs "shared" it and each recast wiped the other (the ping-pong depop). touchMs = last time
// this set was updated -> on overflow we evict the OLDEST set, never an actively-debuffed target.
static const int DEBUFF_SLOTS = 32;
struct DebuffSet { unsigned id = 0; int n = 0; unsigned short ids[16] = {}; unsigned startMs[16] = {}; unsigned baseMs[16] = {}; unsigned char self[16] = {}; unsigned char th = 0; unsigned char lastHpp = 100; unsigned touchMs = 0; };   // lastHpp : last seen HP% of this mob -> detect death (0) / a fresh mob that RECYCLED this server id (near-dead -> near-full = new spawn) so Apex mobs never inherit a dead mob's debuffs   // baseMs[i] : the casting spell's base duration (tb_debuff_gen) ; self[i] : 1 = YOU cast it (exact wear-off + real timer), 0 = another caster (no wear-off -> kept as "???") ; th : Treasure Hunter level applied to this mob (0 = none)

// POINTWATCH (module) : the XP / CP / ML progression bar + Merits, ported from AioHUD's pointwatch engine
// (Byrthnoth's pwcore). 100% packet-fed : 0x061 (char stats : level / EXP / Master Level / Exemplar Points),
// 0x063 Order 2 (merits) + Order 5 (per-job Capacity Points & Job Points), and 0x029/0x02D exp messages for the
// live per-kill increments + the X/h rate. Constants : CP -> next Job Point = 30000, Limit Points -> Merit = 10000.
struct RateReg {   // a ring of recent point gains (ms timestamp + value) -> points/hour, like analyze_points_table
    unsigned t[128] = {0}; int v[128] = {0}; int n = 0;
    void add(int val);
    int  rate() const;   // points/hour over the last <=600 s (0 until ~30 s of data)
};
struct PointWatch {
    int      jobLevel = 0;                 // 0x061 : Main Job Level  -> picks the stage (< 99 = XP)
    int      masterLevel = 0;              // 0x061 : Master Level    -> >= 1 picks the ML/EP stage
    unsigned xpCur = 0, xpTnl = 0;         // 0x061 : Current / Required EXP
    unsigned epCur = 0, epTnml = 0;        // 0x061 : Current / Required Exemplar Points (ML)
    unsigned cpCur = 0; int cpJp = 0;      // 0x063 Order 5 : Capacity Points / Job Points (tnjp = 30000 const)
    unsigned lpCur = 0;                    // 0x063 Order 2 : Limit Points (tnm = 10000 const)
    int      merits = 0, maxMerits = 0;    // 0x063 Order 2 : Merit Points / Max Merit Points
    RateReg  xpReg, cpReg, epReg;          // gain history -> xpRate/cpRate/epRate
    bool     valid = false;                // a 0x061 has arrived at least once this session
    bool     xpMem = false;                // Current/Required EXP read from FFXiMain static data -> the XP row fills on load
    bool     cpMem = false;                // CP/Job Points read from memory (*(g+0x48)) -> the CP row fills on load
    bool     epMem = false;                // Exemplar Points read from FFXiMain static data -> the ML row fills on load
    bool     merMem = false;               // Limit Points/merits read from FFXiMain static data -> the Merits row fills on load
};

// ZONE TRACKER (module) : the PointWatch zone providers, ported from AioHUD pwcore. Dynamis (5 "granules of time"
// key items + a run timer) and Abyssea (7 lights + visitant time), both 100% packet-fed (0x055 KI / 0x02A zone
// messages). Lights index/order + caps : [0]Pearl 230 [1]Azure 255 [2]Ruby 255 [3]Amber 255 [4]Gold 200 [5]Silver
// 200 [6]Ebon 200 (matches the mockup zoneTracker display). Reset on zone change.
struct ZoneTracker {
    int mode = 0;                 // 0 none, 1 Dynamis, 2 Abyssea, 3 Omen, 4 Nyzul, 5 Sheol, 6 Limbus
    int curZone = -1;             // last zone id seen (transition detection)
    // Dynamis
    unsigned dynEntryMs = 0;      // GetTickCount when we entered (timer origin)
    int      dynLimitSec = 3600;  // total run seconds (3600 + KI time-extensions)
    int      dynZone = 0;
    unsigned char ki[5] = {0};    // Crimson / Azure / Amber / Alabaster / Obsidian granules owned
    // Abyssea
    int      abyOffset = 7315;    // 0x02A message base (7215 for zones 215/253, else 7315)
    int      lights[7] = {0};     // Pearl/Azure/Ruby/Amber/Gold/Silver/Ebon
    int      visitantMin = 0;     // visitant time from the last status message (minutes)
    unsigned visitantMs = 0;      // GetTickCount when visitantMin was set -> live MM:SS countdown
    // Omen (mode 3, zone 292) : up to 10 bonus objectives + omen count + bonus timer, parsed from the mode-161
    // objective text (incoming-text callback). type = 1..14 (see omen_short), req = -1 = unknown (loaded mid-floor).
    struct OmenObj { int type = 0; int cur = 0; int req = 0; };
    OmenObj  omen[10];
    int      omens = 0;
    int      omenBonusSec = 0; unsigned omenBonusMs = 0;   // bonus-objective timer -> live MM:SS
    unsigned char omenCleared = 0;
    char     floorObj[48] = {0};   // the floor objective line ("Vanquish ...", "Free Floor!", ...)
    // Nyzul Isle (mode 4, zone 77) : floor / timer / objective tracker + token estimator (ported from the NyzulHelper
    // addon, Glarin of Asura). Fed by the incoming-text callback (modes 123/146/148) + the 0x055 armband KI in the
    // staging point (zone 72). See game-data/zone-tracker.md.
    int      nyFloor = 0, nyStartFloor = 0;    // current / starting floor (relative floor wraps +100 past floor 100)
    int      nyTimerSec = 0; unsigned nyTimerMs = 0;   // set_timer origin (nyTimerMs == 0 -> no timer yet) -> live countdown
    int      nyCompleted = 0, nyPenalties = 0; // floors cleared this run / token penalties on the current floor
    double   nyTokens = 0.0;                    // potential tokens (accumulated per cleared floor)
    int      nyPartySize = 1;
    unsigned char nyArmband = 0;                // Nyzul assault armband (KI 797) -> +10% token rate
    unsigned char nyObjPending = 1;             // 1 = objective pending (yellow) ; 0 = cleared (green)
    unsigned char nyRestrFail = 0;              // 0 = restriction warning (orange) ; 1 = violated (red)
    char     nyObjective[48] = {0};
    char     nyRestriction[40] = {0};
    // Sheol / Odyssey (mode 5, zones 298/279 entered FROM Rabao 247 ; 298/279 are also Selbina HTMBs, hence the
    // from-Rabao gate). segments earned THIS run = the banked 'Mog Segments' currency (0x118 @byte 0x8C) minus the
    // baseline captured at entry. Ported from addons/sheolhelper + AioHUD modules/sheolhelper.lua. sheolzone (A/B/C)
    // + resistances = Phase 2.
    int      sheolzone = 0;         // 0 = unknown, 1/2/3 = Sheol A/B/C
    int      segBank = -1;          // latest banked Mog Segments (from ANY 0x118) ; -1 = never seen -> next read baselines
    int      segBase = -1;          // baseline banked total at run entry ; segments = segBank - segBase
    int      segments = 0;          // segments earned this run
    unsigned char segLastRun = 0;   // 1 = frozen "N (last run)" display back in Rabao after a run
    // Limbus (mode 6, zones 38 Apollyon / 37 Temenos). 0x075 is the generic BATTLEFIELD TIMER/BARS packet, not a
    // "menu" packet : bar[i] sits at +0x28 + i*0x14 as { s32 progress ; char label[16] }. In Limbus the server
    // fills bar0 label = "<Area>_Lv<NNN>" (progress inactive) and bar1 = the floor, e.g. label "SW_Floor_#3" with
    // progress = the on-screen gauge. The client draws that gauge as progress*0.01f, so it IS a percentage
    // (FFXiMain+0x21F9D0). See game-data/zone-tracker.md. Reset on entering the zone.
    char     limbusArea[16] = {0};  // "Apollyon" / "Temenos"
    int      limbusLevel = 0;       // chosen level (e.g. 119)
    int      limbusProgress = -1;   // 0..100 percent (floor bar) ; -1 = unknown -> no progress bar drawn
    char     limbusQuad[8] = {0};   // quadrant from the floor label : "SW" / "NW" / ... ; empty = unknown
    int      limbusFloor = -1;      // floor number ; -1 = unknown -> no floor line drawn
    // Limbus currencies : packet 0x118 (the SAME one carrying Mog Segments @0x8C), Temenos Units @0x98 /
    // Apollyon Units @0x9C -- both shown whichever Limbus zone you stand in. -1 = no 0x118 seen yet.
    int      limbusTemenos = -1;
    int      limbusApollyon = -1;
    // Live run economy, all from 0x02A while in a Limbus zone (0x118 does NOT fire mid-run -- confirmed by a full
    // //aio limbusrun capture : not one 0x118 all run). Message ids are ZONE-RELATIVE and drift across patches
    // (the Sheol handler below documents 40005 -> 40015 -> 40016), so they are matched MASKED :
    //   7247 : "Acquired Apollyon units: <p1>. Remaining: <p2>. Total: <p3>/<p4>"   -> live currency, no 0x118 needed
    //   7288 : "You may collect data <p1> more times"                               -> unique-data allowance
    //   7069 / 7070 : key item gained / lost -- ids 9956..9998, and the NAME carries the floor ("Apollyon SW #4",
    //                 which cross-checks bar1's "SW_Floor_#4" label from the 0x075 block). Bulk 7070 = run over.
    int      limbusUnits = -1;      // your running total (7247 p3) ; -1 = not seen this session
    int      limbusUnitsCap = 0;    // your cap (7247 p4)
    int      limbusUnitBase = -1;   // total BEFORE this run's first payout ; -1 = not baselined yet
    int      limbusRunUnits = 0;    // units banked THIS run = limbusUnits - limbusUnitBase. Derived from the
                                    // AUTHORITATIVE running total (p3), never accumulated from the per-event p1 --
                                    // same reason as segBase/segments above : a duplicated packet cannot double-count
                                    // and a dropped one cannot under-count, whereas a running sum breaks permanently.
    int      limbusCofferAmt = 0;   // last coffer payout (a gain >= LIMBUS_COFFER_MIN, vs ~40-110 per kill)
    char     limbusCofferAt[12] = {0};   // floor it dropped on, e.g. "SW #4"
    int      limbusBigAmt = 0;      // last BIG payout (>= 5000) -- kept separately, it is the one worth remembering
    char     limbusBigAt[12] = {0};
    int      limbusWeekLeft = -1;   // "You may collect data N more times" (7288 p1) = the WEEKLY allowance left
                                    // (5 data collections per week), NOT a per-map counter.
};

// LIMBUS coffer history -- ONE per area (0 = Apollyon, 1 = Temenos), deliberately OUTSIDE ZoneTracker. The zone
// cache is a single file shared by every tracked mode and is only restored when curZone matches, so one Dynamis
// run between two Limbus runs would wipe this. It gets its own file (limbus.dat) and its own lifetime.
// A chip = one coffer opened : the floor it was on + its payout in thousands. Finding a 5k ENDS the cycle (chips
// cleared, only "last 5000 @ floor" kept) ; so does the weekly allowance going back UP (a new week started).
// One slot per QUADRANT, not per weekly collection : an area has four, and a coffer always sits on the last floor
// of one of them (Apollyon NW#5 / SW#4 / NE#5 / SE#4, Temenos N-F7 / W-F7 / E-F7 / C-F4 -- read off the key-item
// table, ids 9956..9998). slotK = that quadrant's payout in thousands, 0 = not opened yet (dim placeholder).
struct LimbusCoffers {
    unsigned char slotK[4] = {0};     // [quadrant] payout in thousands : 0 none, 3 = red, >=5 = green
    int           bigAmt = 0;         // last cycle-ending payout
    char          bigAt[8] = {0};     // and the quadrant it was found in
    int           weekSeen = -1;      // last "collect data N more times" -> N going UP means a new week
};
// Quadrant labels per area, in slot order. Index 0 = Apollyon, 1 = Temenos.
inline const char* limbus_slot_label(int area, int slot) {
    static const char* A[4] = { "NW5", "SW4", "NE5", "SE4" };
    static const char* T[4] = { "N7",  "W7",  "E7",  "C4"  };
    if (slot < 0 || slot > 3) return "";
    return (area == 1) ? T[slot] : A[slot];
}
// "SW" -> 1 for Apollyon, "W" -> 1 for Temenos ; -1 when it matches nothing.
inline int limbus_slot_of(int area, const char* quad) {
    if (!quad || !quad[0]) return -1;
    static const char* A[4] = { "NW", "SW", "NE", "SE" };
    static const char* T[4] = { "N",  "W",  "E",  "C"  };
    for (int i = 0; i < 4; ++i) {
        const char* k = (area == 1) ? T[i] : A[i];
        int j = 0; while (k[j] && quad[j] == k[j]) ++j;
        if (!k[j] && (quad[j] == 0 || quad[j] == '_' || quad[j] == ' ')) return i;
    }
    return -1;
}

// EMPYPOP (module) : the pop items / key items needed to spawn an Abyssea empyrean NM. Each GLOBAL pop is a
// GROUP holding the chain of sub-pops that obtains it ; a group is "obtained" once you hold its global pop.
// NM pop-chain DATA ported from the Empy Pop Tracker addon, (c) 2020 Dean James (Xurion of Bismarck), BSD-3
// (see NOTICE-EmpyPop.txt + nms_gen.h) ; the reading + the rendering are ours. See game-data/key-items.md +
// game-data/inventory.md for the two memory reads this hangs off.
//
// This is DATA, not lines : the upstream Lua baked colour markup into the model, which would drag rendering
// into model/ (layering rule). Widgets colour `owned` / `pool` themselves ; nothing here knows about pixels.
// Capacities are measured over the generated table (worst case chloris : 4 groups / 15 nodes) with headroom ;
// ep_refresh CLAMPS rather than overflowing, so a future regen that grows past them degrades, never corrupts.
struct EmpyPopNode {
    unsigned short id = 0;
    unsigned char  isKI = 0;        // 1 = key item (read via owns_key_item) ; 0 = item (via count_item)
    unsigned char  depth = 0;       // 0 = the group's global pop ; 1+ = a sub-pop, one indent level each
    unsigned char  owned = 0;
    unsigned char  pool = 0;        // copies of this id sitting in the treasure pool right now
    const char*    name = 0;        // resolved from the gen tables (static storage) ; 0 = unknown id
    const char*    fromName = 0;    // "Adamastor, Forced (C-4)" -- free text carrying the map position
};
struct EmpyPopGroup {
    unsigned char obtained = 0;
    unsigned char first = 0, count = 0;   // [first, first+count) into EmpyPop::nodes -- node[first] IS the global pop
};
struct EmpyPop {
    static const int MAX_GROUPS = 8;      // measured max 5 (arch dynamis lord)
    static const int MAX_NODES  = 24;     // measured max 15 (chloris)
    char         key[32] = {0};           // the tracked NM's lookup key, COPIED (longest is "arch dynamis lord").
                                          // Never the caller's pointer : the config owns that buffer and may reuse
                                          // it, which would both dangle and defeat the change check (strcmp of a
                                          // buffer against itself is always 0). "" = nothing tracked.
    const char*  nmName = 0;              // display name ("Briareus") -- static storage, from the generated table
    bool         valid = false;           // false = unknown key / no data -> widget draws nothing
    bool         allDone = false;         // every group obtained -> "READY!"
    EmpyPopGroup groups[MAX_GROUPS];
    int          nGroups = 0;
    EmpyPopNode  nodes[MAX_NODES];
    int          nNodes = 0;
    // Collectable (23 of the 28 NMs have one) : a plain "have N of M" counter, not part of the pop chain.
    unsigned short collId = 0, collTarget = 0;
    unsigned       collCount = 0;
    unsigned char  collPool = 0;
    bool           hasColl = false, collDone = false;
};
// The EmpyPop DEMO chain (chloris, 2 of its 4 key items owned + one pool drop + 30/50 collectable) : the config
// preview / Help sample / edit placeholder. Pure data, zero memory read -> it renders identically on a character
// who owns nothing. Built by the widget ONCE (it never changes) ; lives in model/ because it reads nms_gen.h.
void ep_build_sample(EmpyPop& out);

// Buff timers (Timers module) : EXACT server-sent durations from the 0x063 type-9 packet. `expiry` is an absolute
// FFXI 1/60-second tick ; remaining seconds = (int)(expiry - ffxi_now_tick()) / 60 (the signed diff handles the
// 32-bit wrap). Includes gear / merits / Composure / song duration -- nothing to estimate.
struct BuffTimer { unsigned short id; unsigned expiry; };
unsigned ffxi_now_tick();   // the current time as an FFXI buff tick (epoch 1009810800 + era offset, x60)

struct PartyState {
    PMember m[6];
    int count = 0;
    CastSlot casts_[18];
    WsPopup  wsPop_;                      // your last WS (arcade popup) ; startMs=0 = none yet
    const WsPopup& ws_popup() const { return wsPop_; }
    Resonating reson_[8];                 // skillchain windows per tracked target
    // the FORMED skillchain window on `tid` (or 0) -> the Skillchains widget reads this from the party() singleton.
    const Resonating* skillchain_for(unsigned tid) const {   // any LIVE window on the target (step-1 open OR formed)
        if (tid) for (int i = 0; i < 8; ++i) if (reson_[i].target == tid && reson_[i].nProp) return &reson_[i];
        return 0;
    }
    // The most-recently-updated LIVE resonance on ANY tracked mob (newest openMs, window not yet past). Used as the
    // Skillchains box's last fallback (config scNearby) so a party member's chain shows when your <t>/<bt> has none.
    const Resonating* skillchain_newest_live() const;
    void prune_skillchains();             // once/frame : drop resonance windows whose mob has died (call near refresh_hate)
    void sc_open(unsigned tid, unsigned aid, int res, const unsigned char* props, int delay);   // WS/spell finish -> step-1 resonance (its opening properties)
    void sc_close(unsigned tid, unsigned aid, int res, int prop, int delay);                    // add-effect skillchain -> step+1 (the 1 resulting property)

    TreasureItem treasure_[10];           // the lottery pool, indexed by packet slot 0..9
    const TreasureItem* treasure_slots() const { return treasure_; }   // the TreasurePool widget reads this
    void on_treasure_add(const unsigned char* p);   // 0x0D2 : item added / removed
    void on_treasure_lot(const unsigned char* p);   // 0x0D3 : lot info / won-dropped
    void treasure_clear() { for (int i = 0; i < 10; ++i) treasure_[i] = TreasureItem{}; }   // on zone change

    HateEntry hate_[128];                 // tracked aggro (0x028-fed, sticky) : mob -> PC + last-seen ms. Sole membership
                                          // source for the hate list ; sized for a Crawlers' Nest [S] pull (no ring thrash)
    HateRow   hateRows_[24]; int hateN_ = 0;   // resolved display rows (built once/frame, lowest-HP, sorted HP ascending)
    void refresh_hate();                  // once/frame : resolve tracked mobs to rows + prune (call after set_target_ctx)
    const HateRow* hate_rows(int& n) const { n = hateN_; return hateRows_; }   // the HateList widget reads this
    void hate_clear() { for (int i = 0; i < 128; ++i) hate_[i] = HateEntry{}; hateN_ = 0; }   // on zone change / logout

    // Friendly PETS (avatars / jugs / automatons / trust pets owned by us or a party/alliance member), learned from
    // the pet packets 0x067 (Pet Info) / 0x068 (Pet Status). Needed so a mob SWINGING at our pet is recognised as
    // aggro on the party (a pet id is >= 0x01000000, so the roster check alone can't tell it apart from a mob).
    unsigned petId_[16] = {0};
    unsigned petOwner_[16] = {0};         // the owning PC (roster id) -> shown as the Target column, and the red-claim test
    void on_pet_info(const unsigned char* p);    // 0x067 : Pet ID @+0x08, Owner Index @+0x0C
    void on_pet_status(const unsigned char* p);  // 0x068 : Owner ID @+0x08, Pet Index @+0x0C, Target ID @+0x14
    void pets_clear() { for (int i = 0; i < 16; ++i) { petId_[i] = 0; petOwner_[i] = 0; } }   // on zone change / logout
    bool is_party_or_pet(unsigned id) const;     // self / a roster PC / a tracked friendly pet
    unsigned pet_owner(unsigned id) const;       // if id is a tracked pet -> its owner PC ; else id unchanged

    ZoneTracker zt_;                             // Dynamis / Abyssea zone providers (Zone Tracker module)
    const ZoneTracker& zone_tracker() const { return zt_; }
    LimbusCoffers lc_[2];                        // [0] Apollyon (zone 38), [1] Temenos (zone 37) -- own file, see lc_save
    const LimbusCoffers& limbus_coffers(int area) const { return lc_[(area == 1) ? 1 : 0]; }
    void zt_set_zone(int zone, const char* name);   // called each frame ; detects the Dynamis/Abyssea zone + resets on change
    void zt_recompute_dyn_limit();                  // 3600 + owned-KI time-extensions for the current Dynamis zone
    void on_2a(const unsigned char* p);             // 0x02A : Abyssea zone messages (lights + visitant time)
    void on_55(const unsigned char* p);             // 0x055 : key items -> the Dynamis granules
    void on_118(const unsigned char* p);            // 0x118 currency2 : Mog Segments (@0x8C) -> Odyssey segment run delta (mode 5)
    void on_034(const unsigned char* p);            // 0x034 NPC interaction : Rabao conflux menu -> Sheol A/B/C (sheolzone)
    void on_00e(const unsigned char* p);            // 0x00E NPC update : fallback Sheol A/B/C from a mob's instance bits
    void on_limbus_075(const unsigned char* p);     // 0x075 : Limbus menu -> <Area>_Lv<NNN> @+0x2C (mode 6)
    void on_omen_text(const char* s);               // mode-161 Omen objective text (via the incoming-text callback)
    static const char* omen_short(int type);        // objective type id (1..14) -> short label
    void on_nyzul_text(const char* s, int mode);    // Nyzul Isle text (modes 123/146/148 via the incoming-text callback)
    int  nyzul_remaining() const;                   // live floor-timer seconds left (0 = no timer ; may be < 0, clamp at display)
    void zt_save() const;                           // persist the current zone-tracker run to disk (survives an unload/reload/crash)
    bool zt_load(int zone);                         // restore it for `zone` -> true if a valid same-zone cache loaded (fresh plugin load)
    // Seed a chip by hand : coffers opened BEFORE this feature existed (or before the build was deployed) cannot be
    // recovered from anywhere -- the payout only ever arrives as a live 0x02A. `amtK` >= 5 ends the cycle, exactly
    // as a real 5k would. Returns false on a full/invalid entry.
    bool limbus_add_chip(int area, const char* quad, int amtK);   // quad = "SW" / "NE" / ... (Temenos : "N"/"W"/"E"/"C")
    void limbus_clear_chips(int area) { LimbusCoffers& l = lc_[(area == 1) ? 1 : 0]; for (int i = 0; i < 4; ++i) l.slotK[i] = 0; l.bigAmt = 0; l.bigAt[0] = 0; lc_save(); }
    void lc_save() const;                           // persist the Limbus coffer chips (own file : must outlive the zone cache)
    void lc_load();                                 // restore them (any zone, any time -- no zone match required)

    EmpyPop ep_;                                 // EmpyPop module : the tracked NM's pop chain, resolved against live memory
    const EmpyPop& empypop() const { return ep_; }   // the EmpyPop widget reads this
    void ep_refresh(const char* nmKey);          // call once/frame with the tracked NM key (0 = nothing tracked).
                                                 // SELF-THROTTLES to 2 Hz -- it snapshots the ~101 KB item container --
                                                 // but rebuilds immediately when nmKey changes, so the UI never lags a pick.

    PointWatch pw_;                              // XP / CP / ML + Merits (PointWatch module)
    unsigned char pwMainJob_ = 0;                // 0x061 Main Job id -> indexes the 0x063 Order-5 job-point array
    const PointWatch& pointwatch() const { return pw_; }   // the PointWatch widget reads this
    void on_char_stats(const unsigned char* p);  // 0x061 : level / EXP / Master Level / Exemplar Points
    void on_set_update(const unsigned char* p);  // 0x063 : Order 2 (merits) / Order 5 (Capacity Points + Job Points) / Order 9 (buff timers)

    // --- Timers module : self buff timers (exact durations, from 0x063 type-9) ---
    BuffTimer buffTimers_[32]; int buffTimerN_ = 0;
    const BuffTimer* buff_timers(int& n) const { n = buffTimerN_; return buffTimers_; }
    void buff_timers_clear() { buffTimerN_ = 0; }
    // Timers "self-cast only" filter : who last applied each status ON YOU (server id ; 0 = unknown). Filled by
    // on_action from the 0x028 caster when a buff spell/JA lands on selfId_ ; queried by the Timers box.
    unsigned buffCaster_[1024] = { 0 };
    unsigned buff_caster(unsigned status) const { return status < 1024 ? buffCaster_[status] : 0; }
    // //aio bcaptlog : dump the next N buff actions (cat 4/6/11) landing on self -> reveals the per-action
    // MESSAGE id used to grant a status, so we can add any unrecognised "gains effect" message to on_action.
    int bcaptLog_ = 0;
    void arm_bcapt_log(int n) { bcaptLog_ = n; }
    // Corsair roll : the pip total (1..12, DOUBLE-UP included) + lucky/unlucky flag, keyed by the roll's status.
    // Filled by on_action from the roll's 0x028 (target[0] param = pip). Read by the Timers box to show "Roll (7)".
    unsigned char rollVal_[1024]  = { 0 };
    unsigned char rollLuck_[1024] = { 0 };   // bits0-1 : 0 normal / 1 lucky / 2 unlucky ; bit2 (0x04) : cast under Crooked Cards
    unsigned short lastRollStatus_ = 0, lastRollAid_ = 0;   // the roll being built -> Double-Up (abil 123, no status) updates ITS pip
    // BRD song : which song-enhancing JAs were UP when you cast it, keyed by SPELL id (NOT status -- two same-family
    // songs like Advancing + Victory March share a status but differ by spell). bit0 Soul Voice, bit1 Nightingale,
    // bit2 Troubadour, bit3 Marcato. Shown as compact tags (SV)(N)(T)(M) on the song's Timers row.
    unsigned char songMod_[1024] = { 0 };
    unsigned char song_mods(unsigned spellId) const { return spellId < 1024 ? songMod_[spellId] : 0; }
    struct RollInfo { unsigned char value, luck, cc; };
    RollInfo roll_info(unsigned status) const { RollInfo r{ 0, 0, 0 }; if (status < 1024) { r.value = rollVal_[status]; r.luck = rollLuck_[status] & 3; r.cc = (rollLuck_[status] >> 2) & 1; } return r; }
    // true if your CURRENT main/sub job (or a usable job ability) could itself grant `status` -- i.e. this buff
    // COULD be a self-cast. Lets the "self-cast only" filter hide a buff of UNKNOWN caster that your job can never
    // produce (a Trust's Protectra / Geo-Haste on a Bard) while keeping ones it can (your own songs / rolls).
    bool self_can_produce_buff(unsigned status, const unsigned char* jaBits, bool jaOk) const;
    // classify an UNKNOWN-caster buff by WHO in the party/alliance could have produced it, from their jobs (a buff
    // spell granting `status`). Lets the source filter tell a Trust's buff (e.g. Sylvie's Geo bubble : only a GEO can
    // make it, and she's the only GEO -> trust) from a dual-box player's buff, WITHOUT having seen the cast.
    void buff_source_jobs(unsigned status, bool& playerCan, bool& trustCan) const;
    // the SPELL that last granted each status ON YOU (set by on_action when a buff you cast hits self) -> lets
    // the self buff-timer rows show the spell name/tier (e.g. "Valor Minuet V") instead of the bare status ("Minuet").
    unsigned short selfBuffSpell_[1024] = { 0 };
    unsigned short self_buff_spell(unsigned status) const { return status < 1024 ? selfBuffSpell_[status] : 0; }
    // ring of your recent self-cast buffs (status+spell+tick) -> lets two same-status buffs (Minuet V + IV) be told
    // apart : the self buff-timer that expires LATEST is the most-recent cast. Matched by expiry rank in self_buff_spell_ranked.
    struct SelfCast { unsigned short status = 0; unsigned short spell = 0; unsigned tick = 0; };
    SelfCast selfCasts_[24]; int selfCastHead_ = 0;
    unsigned short self_buff_spell_ranked(unsigned short status, unsigned expiry) const;
    unsigned self_id() const { return selfId_; }
    int self_main_job() const { for (int i = 0; i < count; ++i) if (m[i].id == selfId_) return m[i].mjob; return 0; }   // current MAIN job id (Timers "track per job" filter) ; 0 if unknown
    bool is_trust(unsigned id) const { for (int i = 0; i < count; ++i) if (m[i].id == id) return m[i].isTrust != 0; return false; }   // a caster id -> is it a Trust NPC ? (Timers buff-source filter)
    // --- Timers module : BUFFS YOU cast on OTHER players (person name + ESTIMATED timer). The client sends
    //     NO per-buff timer for other players, so on_action estimates from tb_buff_gen's base duration when a
    //     buff spell (0x028 cat 4) you cast lands on an ally. Keyed by (target id, status) ; refreshed on recast.
    struct OtherBuff { unsigned target = 0; unsigned short status = 0; unsigned short spell = 0; unsigned startMs = 0; unsigned durMs = 0; unsigned expTick = 0; unsigned char seen = 0; unsigned char mirrorSelf = 0; unsigned char isAbil = 0; unsigned char aoe = 0; char name[20] = {0}; };   // isAbil : `spell` holds an ABILITY id (COR roll) ; aoe : the cast hit >=2 targets (Protectra / a spell under SCH Accession) -> a REAL AoE, group it
    OtherBuff otherBuffs_[32]; int otherBuffN_ = 0;
    unsigned obZone_ = 0xFFFFFFFFu, obZoneGraceMs_ = 0;   // zoning grace : after a zone change the 0x076 buff lists re-populate over a
                                                          //   few seconds ; during the grace we KEEP ally buffs on their estimate (they
                                                          //   persist across a zone) instead of dropping them as "worn" on an empty cache.
    bool obZoneGrace_ = false;                            // set by prune_other_buffs_worn ; the DRAWER reads it (in_zone_grace) to show
                                                          //   ally / AoE rows from the estimate without the (empty) real-buff cross-check.
    bool in_zone_grace() const { return obZoneGrace_; }
    const OtherBuff* other_buffs(int& n) const { n = otherBuffN_; return otherBuffs_; }
    void other_buffs_clear() { otherBuffN_ = 0; }
    void clear_other_buffs_for(unsigned id) { int w = 0; for (int k = 0; k < otherBuffN_; ++k) if (otherBuffs_[k].target != id) { if (w != k) otherBuffs_[w] = otherBuffs_[k]; ++w; } otherBuffN_ = w; }
    // --- JOB-CHANGE detection (Timers) : a member (self or ally) that swaps main/sub drops ALL buffs and resets
    //     recasts (except the SP 2-hr, whose recast is shared across jobs -> read live, unaffected). We shadow each
    //     member's (mjob,sjob) ; on a change we clear their tracked ally buffs + list the id so the box drops their
    //     FOCUS-monitor rows (no false "missing" alert on a deliberate job change). ---
    struct JobShadow { unsigned id; unsigned char mj, sj; };
    JobShadow jobShadow_[24]; int jobShadowN_ = 0;
    unsigned jobChanged_[24]; int jobChangedN_ = 0;
    int job_changes(unsigned* out, int maxN) const { int n = jobChangedN_ < maxN ? jobChangedN_ : maxN; for (int i = 0; i < n; ++i) out[i] = jobChanged_[i]; return n; }
    void note_member_job(const PMember& mm) {   // called per member each load ; records the change into jobChanged_
        if (!mm.id) return;
        if (mm.mjob < 1 || mm.mjob > 22) return;   // job UNREADABLE (member out of zone / mid loading-screen reports 0) -> NOT a real
                                                   //   job change : ignore it, else zoning would falsely "reset" and wipe the member's tracked ally buffs.
        int s = -1; for (int i = 0; i < jobShadowN_; ++i) if (jobShadow_[i].id == mm.id) { s = i; break; }
        if (s < 0) { if (jobShadowN_ < 24) { s = jobShadowN_++; jobShadow_[s].id = mm.id; jobShadow_[s].mj = (unsigned char)mm.mjob; jobShadow_[s].sj = (unsigned char)mm.sjob; } return; }
        if (jobShadow_[s].mj != (unsigned char)mm.mjob || jobShadow_[s].sj != (unsigned char)mm.sjob) {
            jobShadow_[s].mj = (unsigned char)mm.mjob; jobShadow_[s].sj = (unsigned char)mm.sjob;
            if (jobChangedN_ < 24) jobChanged_[jobChangedN_++] = mm.id;
            if (mm.id != selfId_) clear_other_buffs_for(mm.id);   // an ally's buffs dropped -> drop the tracked rows (self keeps none here)
        }
    }
    // zoning : true between the 0x00B (zone-out -> loading screen) and the next 0x00A (zone-in, new zone ready).
    // Windower derives its own `zoning` flag the same way. The HUD gate hides every box while this is set.
    void set_zoning(bool z) { zoning_ = z; }
    bool is_zoning() const { return zoning_; }
    // remaining seconds of the caster's OWN copy of `status` (exact 0x063 self timer), or -1 if not present.
    // Used to mirror an EXACT timer onto an AoE buff you cast that ALSO landed on yourself (songs, -ra, Accession).
    int self_buff_remaining(unsigned short status) const;
    unsigned self_buff_expiry(unsigned short status) const;   // the caster's own 0x063 expiry (FFXI ticks) for `status`, 0 if none
    void prune_other_buffs_worn();   // drop entries whose target no longer shows the status (0x076) -> early wear-off
    // --- GEO self Indi- aura : a GEO carries ONE Indi- ; the effect status in 0x063 is REFRESHED every ~3s by the
    //     aura pulse, so it never shows the real aura lifetime. When YOU cast an Indi- on yourself we compute the
    //     aura duration (base + JP 1362 + Indicolure gear) and show THAT instead, keyed by the effect status. ---
    struct GeoAura { unsigned short status = 0; unsigned short spell = 0; unsigned expTick = 0; };
    GeoAura selfGeo_{};                                                   // the single Indi- you are carrying (status 0 = none)
    unsigned entrustTick_ = 0;                                            // GetTickCount when Entrust (JA 386) was used -> the NEXT Indi- is a fixed buff on an ally
    bool     zoning_ = false;                                             // between 0x00B (zone-out) and 0x00A (zone-in) -> HUD hidden
    void record_geo_aura(unsigned short status, unsigned short spell, unsigned expTick) { selfGeo_.status = status; selfGeo_.spell = spell; selfGeo_.expTick = expTick; }
    int geo_aura_remaining(unsigned short status) const;                 // computed seconds left for a self Indi- you cast, -1 if none/expired
    const GeoAura& self_geo() const { return selfGeo_; }
    const char* pc_name_by_id(unsigned id) const;   // roster / alliance server id -> player name (0 if unknown)
    int party_order(unsigned id) const;             // roster position (0..5 party, 6..17 alliance ; 99 unknown) -> sort order
    void on_exp_msg(const unsigned char* p, unsigned id);   // 0x029 / 0x02D : live gains (Param1) + X/h rate
    BuffSet  buffs_[18];
    DebuffSet tdebuffs_[DEBUFF_SLOTS];    // debuffs per tracked target id (roomy : AoE/-ga/Horde songs debuff a whole same-name pack at once)
    unsigned  curTarget_ = 0, selfId_ = 0;   // context for on_action : the mob you're on + your own id
    float     selfX_ = 0.0f, selfZ_ = 0.0f;  // the player's own horizontal position (entity X@+0x04 / Z@+0x0C) -> target distance
    unsigned  encumber_ = 0;              // 0x01B 'Encumbrance Flags' (bit sid = equip slot sid is locked) -> equipment viewer cross
    unsigned  learnedMs_[256] = {0};      // LEARNED real debuff durations per status id : recorded on the 0x029 wear-off
                                          // (the actual lifetime), reused for the timer next time -> the countdown self-tunes as you fight

    // Live alliance roster (parties 2 & 3 = member-array slots 6..11 and 12..17), filled by
    // load_from_memory each frame. alli_[0..5] = alliance party 2, alli_[6..11] = party 3.
    PMember alli_[12];
    int      alliN_[2] = { 0, 0 };        // live member count for alliance party 2 / 3

    void on_dd(const unsigned char* p);   // 0x0DD : member update (name/jobs/HP/MP/TP/%) -> also caches
    void on_df(const unsigned char* p);   // 0x0DF : vitals update (HP/MP/TP, refresh %)
    void on_action(const unsigned char* p); // 0x028 : begin/finish casting -> cast bar + landed target debuffs
    void on_029(const unsigned char* p);  // 0x029 : action message -> msg 204 = a status wore off a target (remove its icon)
    void on_076(const unsigned char* p);  // 0x076 : party-member status icons (buffs) -> buffs_[]
    void on_01b(const unsigned char* p);  // 0x01B : job info -> caches the 'Encumbrance Flags' bitfield (locked equip slots)
    unsigned encumbrance() const { return encumber_; }   // bit sid set = equip slot sid is locked (equipment viewer cross)
    const BuffSet* buffs_for(unsigned id) const;   // a member's buffs (null if none cached)
    // per-frame context for the debuff tracker (set by the HUD from the snapshot) : the current
    // target id (the mob debuffs are attributed to) and the local player id (only YOUR debuffs are tracked).
    void set_target_ctx(unsigned targetId, unsigned selfId) { curTarget_ = targetId; selfId_ = selfId; }
    // Derived-state CACHE : the process (pol.exe) survives a plugin //unload+reload, so GetTickCount / the FFXI clock
    // stay valid -- we persist the info we CAN'T re-derive without re-seeing the 0x028 (roll pips, song modifier tags,
    // buff casters, buffs-on-allies for AoE grouping) to a per-character file, and restore it on reload. Live 0x063/
    // 0x076 + pruning drop anything stale. Freshness-guarded (ignored if > 120 s old : game restart / long gap).
    bool cacheLoaded_ = false; unsigned lastCacheSaveMs_ = 0;
    void save_cache(unsigned selfId) const;
    void load_cache(unsigned selfId);
    // active (non-expired) debuff status ids on target `id`, newest last. Fills `out` (status ids) and, if
    // non-null, `remainSec` (approx seconds left = base duration - elapsed ; exact removal comes from the 0x029
    // wear-off). Returns the count written.
    int  target_debuffs(unsigned id, unsigned short* out, int* remainSec, unsigned char* isSelf, int maxN) const;   // remainSec = -1 -> show "???"
    int  target_th(unsigned id) const;   // Treasure Hunter level applied to target `id` (0 = none / unknown)
    void clear_debuffs(unsigned id);     // drop a mob's tracked debuffs (on its death) so a recycled server id starts clean
    void set_debuff_trace(int n);        // //aio dbflog : log the next N target-debuff mutations (add/reset/wipe/wake) to aiohud_debug.log
    void note_mob_hp(unsigned id, int hpp);   // per-frame HP feed for a mob : clears its debuffs on death (hpp<=0) or when a fresh mob RECYCLED this id (was near-dead, now near-full)
    unsigned debuff_dur_ms(unsigned status) const;   // LEARNED real duration if observed, else the base estimate
    int  alliance_count(int tier) const;           // live member count for alliance box tier 1 / 2
    const PMember& alliance_member(int tier, int i) const;   // member i of alliance box tier 1 / 2
    int  find(unsigned id) const;
    // current cast for member `id` : returns the spell name (or 0 if not casting / expired) and
    // fills `pctOut` with the 0..1 cast progress. Used by the party UI's cast line/bar.
    const char* cast_label(unsigned id, float& pctOut, float& alphaOut, int* kindOut = 0) const;   // kindOut (optional) : 0 spell, 1 WS/ability/TP move

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
// party demo member count (1..6). Lets the demo preview the adaptive party height / mask / Set-Ref
// growth at any count.
int  party_demo_count();
void set_party_demo_count(int c);
// demo member count for alliance tier 1 / 2 (1..6 ; 0 for any other tier).
int  party_demo_alliance_count(int tier);
// //aio party N : ONE total count (0..18) -> level + per-box counts (player always member #1 ; party
// fills first, then alliance 1, then alliance 2). 0 = back to live data.
void set_party_demo_total(int total);
// demo "selection" : when on, the demo boxes show a target cursor cycling through all members
// (party then alliances) so the config preview demonstrates the selection highlight. Off in real play.
bool demo_select();
void set_demo_select(bool on);
// TEST helper : append N fake members to the LIVE party (0..5) -> //aio sim N. Lets you watch the box
// grow + the alliances react to the main-party size without needing real players. 0 = off (real size).
int  party_sim_extra();
void set_party_sim_extra(int n);

} // namespace aio
