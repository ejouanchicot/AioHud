// gamestate.h -- the per-frame SNAPSHOT of game data the HUD draws from.
//
// This is the SEAM between "where the numbers come from" and "how they look".
// `poll_game_state()` (model/game_mem.cpp) fills it ONCE per frame from live memory;
// the HUD hands it to every widget via Frame::game. Widgets read from here and NEVER
// touch game memory in draw() -> each datum is read once no matter how many widgets
// use it, every offset/pointer-chain lives in the single poller, and rendering stays
// pure (no SEH, deterministic, testable). The party ROSTER is the one exception: it
// stays in the party() singleton (also refreshed once per frame by load_from_memory),
// referenced here so widgets reach it through the same snapshot.
#pragma once
#include "model/game_mem.h"   // PlayerInfo
#include "model/vana_clock.h" // VanaClock (Minimap box clock header)

namespace aio {

// SCHOLAR GRIMOIRE (module : Grimoire/ScHud). Computed once/frame in the poller : visible only for a SCH main/sub,
// the active Arts (book Light/Dark + Addendum aura) from the player buffs, and the stratagem charges + recast timer
// (ability recast 231 vs the level/JP-derived interval). Reversed from AioHUD targetbar/sch.lua.
struct GrimoireState {
    bool visible = false;     // SCH main, or SCH sub (unless restricted) -> show the book
    int  book = 0;            // 0 = Light Arts book, 1 = Dark Arts book
    bool addendum = false;    // Addendum: White/Black active -> the pulsing aura
    bool dim = false;         // no Arts active -> a dim/inactive book
    bool closed = false;      // NO Arts / Addendum at all -> show the CLOSED-book texture, no charge/recast pastilles
    int  charges = 0;         // 0..5 stratagems available
    int  timerSec = -1;       // seconds to the next charge (-1 = full, no timer)
};

struct GameState {
    bool  inGame = false;     // poll succeeded (player struct populated) ; false = use demo/last-good

    // --- local player (the liquid bars use the fractions ; the party self-row uses `me`) ---
    float hp = 0.50f;         // 0..1  (fraction of max HP)
    float mp = 0.25f;         // 0..1  (fraction of max MP)
    float tp = 0.80f;         // 0..1  (TP / 3000)
    PlayerInfo me;            // full self : id / name / hp / mp / tp / hpp / mpp / jobs
    float meSpeed = 0.0f;     // own movement_speed field (self entity +0x98) ; base 5.0 = 0% (STATIC, reflects gear) -> Player Hub speed band
    unsigned meGil = 0;       // own gil (*( *(g+0x50) + 0x04 )) -> Player Hub gil band
    EquipSet equip = {};      // 16 equipped item ids (0 = empty) + counts -> Player Hub Equipment Viewer grid
    bool equipValid = false;  // the equip read actually resolved (item containers ready) : false during a zone / not-logged-in
                              // -> the viewer must NOT clobber its cached icons on a not-ready read (persist, like the addon does across a zone)

    // --- minimap : current zone, self world position + heading, and the zone's map calibration record ---
    unsigned  zone = 0;       // current zone id (*(g+0x40)+0x02)
    float     meX = 0.0f, meZ = 0.0f;   // self world position (entity+0x04 / +0x0C)
    float     meHeading = 0.0f;         // self facing (entity+0x18, radians)
    MapRecord map;            // (zone, submap 0) map-pixel calibration ; map.valid=false when the zone has no map
    MapEntity mapEnts[MAP_ENT_MAX];   // nearby entities (PC/NPC/mob) for the minimap markers, snapshot once/frame
    int       mapEntN = 0;
    VanaClock vana;           // Vana'diel time / elemental day / moon phase (Minimap box clock header)

    // --- local player buffs (status-icon ids), snapshot once/frame (read_player_buffs, player+0x1C) ---
    unsigned short buffs[32] = { 0 };
    int            nbuff = 0;   // count of valid ids in buffs[]
    bool           buffsOk = false;   // the buff read SUCCEEDED -> nbuff==0 means "no buffs", NOT "no data". Callers must not fail open on an empty list (that made the Timers FOCUS alert unreachable).

    // --- active RECASTS (Timers module) : job-ability (kind 0) + spell (kind 1) cooldowns, snapshot once/frame from
    //     the client recast tables (g+0x22C/0x230 abilities, g+0x234 spells). recastId -> name via the gen tables. ---
    struct RecastEntry { unsigned short recastId; unsigned char kind; int sec; };
    RecastEntry recasts[40];
    int         nRecast = 0;

    // --- selection (the party cursor) : server-ids of <t> (main) and <st> (sub) ---
    unsigned targetId = 0, subTargetId = 0;
    unsigned battleTargetId = 0;     // <bt> : the engaged mob (target_t+0x7C), held even when the reticle <t> is off it, 0 on disengage
    bool     targetLocked = false;   // the main target <t> is LOCKED on (target_t+0x5C bit 0) -> lock symbol

    // --- the ACTIVE target's entity (Target HUD module) : name + HP% + id/index. valid=false when
    //     nothing is targeted. Filled from read_target_entity (//aio tent, 2026-07-03). ---
    TargetEntity target;
    TargetEntity subTarget;          // the <st> sub-target's entity (valid only while a sub-target cursor is up)
    bool         hasSubTarget = false;

    // --- leadership (server-id matches against allianceinfo_t ; 0 = role absent) ---
    unsigned allianceLeader = 0, partyLead1 = 0, partyLead2 = 0, partyLead3 = 0;

    // --- in-game action menu (drives the cost/next box) : type 1=spell 2=JA 3=WS, 0=none ---
    int      menuType = 0;
    unsigned menuAction = 0;
    unsigned menuCursor = 0;   // the menu's 1-based highlight index (+0x4C) -> used to detect a STALE examine
                               // value : if the cursor moves but menuAction stays frozen, the value is ghost.
    unsigned examAbilRaw = 0;  // RAW ability examine, read EVERY frame : a CHANGE = the game examined a real
                               // ability (live selection). (Magic uses menuExamValid, not this -- see below.)
    bool     menuExamValid = false;   // the menu's shared examine-DESCRIPTION object (*(mptr+0x0C)) holds real
                               // data (len@+0x30 != 0, sentinel@+0x34 != 0xFFFFFFFF) : true for a real highlighted
                               // spell/trust, false for a no-magic job's EMPTY magic list (and the category level).
                               // This is the structural "is there an examinable item" gate -- works on OPEN and on
                               // RE-OPEN of the same item, where the frozen-examine-value test cannot.

    GrimoireState grimoire;   // SCH grimoire (book + charges + timer), computed once/frame

    // --- party-window picker (Menu>Party>Distribution>Quartermaster/Lottery, remove member, ...) :
    //     the focused menu "partywin" carries a 1-based cursor index at +0x4C -> the hovered member
    //     is the main party's row (index-1). 0 = no picker open. Reversed 2026-06-28 via //aio pcur. ---
    int      partyMenuSel = 0;
};

// Fill `gs` from live game memory for THIS frame (one read of each pointer-chain). Sets
// gs.inGame=false and leaves the rest untouched when the player struct isn't ready (zoning).
void poll_game_state(GameState& gs);

} // namespace aio
