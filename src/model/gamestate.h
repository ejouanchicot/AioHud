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

namespace aio {

struct GameState {
    bool  inGame = false;     // poll succeeded (player struct populated) ; false = use demo/last-good

    // --- local player (the liquid bars use the fractions ; the party self-row uses `me`) ---
    float hp = 0.50f;         // 0..1  (fraction of max HP)
    float mp = 0.25f;         // 0..1  (fraction of max MP)
    float tp = 0.80f;         // 0..1  (TP / 3000)
    PlayerInfo me;            // full self : id / name / hp / mp / tp / hpp / mpp / jobs

    // --- selection (the party cursor) : server-ids of <t> (main) and <st> (sub) ---
    unsigned targetId = 0, subTargetId = 0;
    bool     targetLocked = false;   // the main target <t> is LOCKED on (target_t+0x5C == 1) -> lock symbol

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

    // --- party-window picker (Menu>Party>Distribution>Quartermaster/Lottery, remove member, ...) :
    //     the focused menu "partywin" carries a 1-based cursor index at +0x4C -> the hovered member
    //     is the main party's row (index-1). 0 = no picker open. Reversed 2026-06-28 via //aio pcur. ---
    int      partyMenuSel = 0;
};

// Fill `gs` from live game memory for THIS frame (one read of each pointer-chain). Sets
// gs.inGame=false and leaves the rest untouched when the player struct isn't ready (zoning).
void poll_game_state(GameState& gs);

} // namespace aio
