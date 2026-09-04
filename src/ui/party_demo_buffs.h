// party_demo_buffs.h -- the buff set the DEMO / config-preview party rows carry.
//
// Its own header, and not a file-static in party.cpp, for ONE reason : the offline test suite can include it.
// party.cpp cannot be linked into the tests (it needs a live D3D8 device), and the invariant below is exactly
// the kind that rots without anyone noticing -- it already did, see the note on the pool.
#pragma once

namespace aio {

// demo buff set (-> something realistic to render in //aio demo AND in the config preview).
// It must hold at least ONE status from EVERY display group (buff_groups.h). The preview draws these rows
// through the SAME sort as the live HUD, so a group with no representative here is a row in the "Buff order"
// list that the user can move up and down while nothing on screen moves -- the setting would look broken when
// it is the sample that is empty. The old pool had no Sneak/Invisible/Deodorize at all (the very group the
// ordering was asked for), nor any Geomancy, Rune, Samba or unclassified status.
// Deliberately INTERLEAVED rather than pre-grouped, for two reasons : it is what the game actually sends (the
// 0x076 list is acquisition order, with no grouping of any kind), and a pre-sorted pool would render the same
// whether the sort ran or not -- so it could not show a regression. Every id below was checked against
// buff_atlas.raw : all 32 cells carry a real icon, none renders as a hole.
// NOTE: FFXI collapses ALL Minuets to status 198 and ALL Marches to 214, so Minuet IV/V share one icon.
// (Dia 134 is a near-white icon -> looks nearly blank, which is the game's own art, not a missing cell.)
static const unsigned short BUFF_POOL[] = {
     40,  71,  43, 214, 134, 317, 541,  33,   // Protect, Sneak, Refresh, Honor March, Dia, Chaos Roll, Indi-Refresh, Haste
    251, 523,  69,  41, 370, 198,  13,  94,   // Food, Ignis, Invisible, Shell, Haste Samba, Valor Minuet, Slow, Enfire
     44, 116, 321,  37,  70, 535,  42,   3,   // Mighty Strikes, Phalanx, Samurai Roll, Stoneskin, Deodorize, Valiance, Regen, Poison
    178, 253,  36,  46, 581,  39,  32, 252    // Firestorm, Signet, Blink, Hundred Fists, Flurry, Aquaveil, Flee, Mounted
};
static const int BUFF_POOL_N = (int)(sizeof(BUFF_POOL) / sizeof(BUFF_POOL[0]));

} // namespace aio
