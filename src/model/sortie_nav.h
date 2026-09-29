// sortie_nav.h -- Sortie NAVIGATION : which wing you are in, where that wing's NM is, where its bitzer is.
//
// Pure tables + arithmetic (no game, no clock) so the whole decision is testable offline. MEASURED on the two full
// runs recorded 2026-09-14 (tapes sortie_tetsouo_* / sortie_kaories_*, zone 133 Outer Ra'Kaznar [U2]) -- every value
// below appears in both tapes.
//
//   * WING : the server's own 0x065 "repositioning" puts you at a FIXED arrival point every time you take a bitzer
//     or a gate. Each point names the wing you arrive in (18 points) or the boss arena (2).
//   * NM : one tracked NM per wing, at a fixed entity index. Its position comes from the server's Widescan track
//     replies (0x0F5, every ~0.44 s while a track runs -- 711 replies for ONE request in the tape) and, when it is in
//     range, from its own 0x00E updates. AioHUD never ASKS : the request is the aioupdate addon's job, opt-in.
//   * BITZER : the four basement bitzers never move (same position on every pass, both clients).
//
// Coordinates are the packets' : X @0x04 (east +), Y @0x0C (north +), the height @0x08 is ignored. The player's own
// X / Y are GameState::meX / meZ (entity +0x04 / +0x0C) -- the same two axes.
#pragma once
#include <math.h>

namespace aio {

static const int SORTIE_NAV_ZONE = 133;   // Outer Ra'Kaznar [U2] -- the only Sortie zone measured
static const int SORTIE_WINGS = 8;         // A..H
static const int SORTIE_BOSS_ROOM = 8;     // sortie_wing_for_arrival : a boss arena, not a wing

struct SortieWing { char letter; unsigned short nmIndex; const char* nm; bool basement; float bzX, bzY; };
static const SortieWing SORTIE_WING[SORTIE_WINGS] = {
    { 'A', 144, "Abject Obdella",       false, 0.0f,   0.0f   },
    { 'B', 223, "Biune Porxie",         false, 0.0f,   0.0f   },
    { 'C', 285, "Cachaemic Bhoot",      false, 0.0f,   0.0f   },
    { 'D', 373, "Demisang Deleterious", false, 0.0f,   0.0f   },
    { 'E', 427, "Esurient Botulus",     true,  338.6f, 147.4f },   // Diaphanous Bitzer (E), entity 837
    { 'F', 498, "Fetid Ixion",          true,  773.2f, 306.8f },   // (F) 838
    { 'G', 552, "Gyvewrapped Naraka",   true,  881.4f, -1.6f  },   // (G) 839
    { 'H', 622, "Haughty Tulittia",     true,  707.3f, -372.6f },  // (H) 840
};

// 0x065 arrival point -> wing 0..7, SORTIE_BOSS_ROOM, or -1 (not a known arrival : keep the wing you had).
struct SortieArrival { float x, y; int wing; };
static const SortieArrival SORTIE_ARRIVAL[] = {
    { -836.0f,  -20.0f, 0 }, { -460.0f,   96.0f, 0 }, { -900.0f,  416.0f, 0 }, { -460.0f,   35.5f, 0 },   // A : entrance, bitzer, arena exit, basement E exit
    { -344.0f,  -20.0f, 1 }, {  -24.0f,  420.0f, 1 }, { -404.5f,  -20.0f, 1 },                        // B
    { -460.0f, -136.0f, 2 }, {  -20.0f, -456.0f, 2 }, { -460.0f,  -75.5f, 2 },                        // C
    { -576.0f,  -20.0f, 3 }, { -896.0f, -460.0f, 3 }, { -515.5f,  -20.0f, 3 },                        // D
    {  580.0f,   31.5f, 4 }, {  280.0f,  276.0f, 4 }, {  186.5f,  -20.0f, 4 },                        // E : enter, arena exit, Aminon exit
    {  631.5f,  -20.0f, 5 }, {  876.0f,  280.0f, 5 },                                                  // F
    {  580.0f,  -71.5f, 6 }, {  880.0f, -316.0f, 6 },                                                  // G
    {  528.5f,  -20.0f, 7 }, {  284.0f, -320.0f, 7 },                                                  // H
    {  624.0f, -620.0f, SORTIE_BOSS_ROOM }, { 184.0f, -660.0f, SORTIE_BOSS_ROOM },                    // the arenas
};
inline int sortie_wing_for_arrival(float x, float y) {
    for (const SortieArrival& a : SORTIE_ARRIVAL)
        if (fabsf(a.x - x) < 0.6f && fabsf(a.y - y) < 0.6f) return a.wing;   // the server sends these to the tenth
    return -1;
}
inline int sortie_wing_for_nm(unsigned index) {
    for (int w = 0; w < SORTIE_WINGS; ++w) if (SORTIE_WING[w].nmIndex == index) return w;
    return -1;
}

// Horizontal distance, in yalms.
inline float sortie_dist(float fromX, float fromY, float toX, float toY) {
    const float dx = toX - fromX, dy = toY - fromY;
    return sqrtf(dx * dx + dy * dy);
}
// Eight-point compass bearing FROM (fromX,fromY) TO (toX,toY) : 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW.
// +Y is north, +X east (the game's map convention).
inline int sortie_cardinal(float fromX, float fromY, float toX, float toY) {
    const float dx = toX - fromX, dy = toY - fromY;
    if (dx == 0.0f && dy == 0.0f) return 0;
    float deg = atan2f(dx, dy) * 57.29578f;   // clockwise from north
    if (deg < 0.0f) deg += 360.0f;
    return ((int)((deg + 22.5f) / 45.0f)) & 7;
}

}  // namespace aio
