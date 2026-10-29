// geo_state.h -- the Geomancer module's packet side and the state it keeps between frames (model/geo_state.cpp).
// The per-frame part (the luopan's entity, the aura, MP, recasts, buffs -> GameState::geo) is compute_geo, in
// game_mem.cpp beside compute_bst. SPEC docs/design/geo-module-spec.md.
//
// The luopan is OUR PET, read exactly as the BST box reads its pet (bst_state.cpp : 0x068 owner = us, or the self
// entity's pet_index) ; what this file adds is what the pet packets do not say : WHICH Geo- it carries (our last
// Geo- cast that finished), how long it has left (the slope of its HP), and the Indi- we handed to an ally (Entrust).
#pragma once

namespace aio {

struct GameState;

// The luopan's remaining life, from the slope of its HP%. A luopan loses HP every tick (perpetuation cost, BG-Wiki
// "Luopan" : -24 HP/tic, Lasting Emanation -17, Ecliptic Attrition -30, the Bagua Charm less), so its HP% falls in a
// straight line whatever the gear, Lasting Emanation or Ecliptic Attrition -- measuring that line follows all of them
// without modelling any. HP% is a whole number, so the line is drawn between the INSTANTS it changed (a step), never
// between the first reading and now : the first reading came at an unknown point inside its step, and a slope taken
// from it would be off by up to a whole percent over the shortest windows. Until two changes MIN_MS apart have been
// seen there is no number ("—" in the box). HP going UP (Life Cycle, a cure) restarts the measurement from that
// instant : the old line no longer describes this luopan.
struct LuopanSlope {
    static const unsigned MIN_MS = 6000;   // ~6 s of measurement before a first estimate (SPEC §2)
    int      lastHpp = -1;                 // the last reading
    unsigned aMs = 0; int aHpp = -1;       // the first change seen in this run (the line's start)
    unsigned bMs = 0; int bHpp = -1;       // the latest change (the line's end)
    void reset() { *this = LuopanSlope{}; }
    void sample(int hpp, unsigned nowMs) {
        if (hpp < 0) return;
        if (lastHpp < 0) { lastHpp = hpp; return; }                        // where the run starts : not a change
        if (hpp > lastHpp) { lastHpp = hpp; aMs = nowMs; aHpp = hpp; bMs = 0; bHpp = -1; return; }   // Life Cycle : a new line from here
        if (hpp == lastHpp) return;
        lastHpp = hpp;
        if (aHpp < 0) { aMs = nowMs; aHpp = hpp; } else { bMs = nowMs; bHpp = hpp; }
    }
    // seconds left (ceil), -1 while the measurement is too short to say. Counted down from the last change, so the
    // figure keeps moving between two steps instead of freezing on each.
    int remaining_sec(unsigned nowMs) const {
        if (aHpp < 0 || bHpp < 0 || bHpp >= aHpp) return -1;
        const unsigned span = bMs - aMs;
        if (span < MIN_MS) return -1;
        const double msPerPct = (double)span / (double)(aHpp - bHpp);
        double left = (double)bHpp * msPerPct - (double)(nowMs - bMs);
        if (left < 0.0) left = 0.0;
        return (int)((left + 999.0) / 1000.0);
    }
};

// What the packets and the frames taught about our luopan and our Entrust.
struct GeoTrack {
    unsigned short bubbleSpell = 0;   // our last Geo- cast that finished (geo_spells_gen.h id) ; 0 = unknown
    unsigned bubbleCastMs = 0;
    unsigned luopanId = 0;            // the luopan entity this bubble belongs to, once seen ; 0 = not seen yet
    unsigned spawnMs = 0;             // when it was first seen
    bool     spawnKnown = false;      // first seen right after our cast : its age is known (the 10-minute cap applies)
    LuopanSlope slope;
    unsigned short entrustSpell = 0;  // the Indi- that landed on an ally (an Indi- cast on another player = Entrust)
    unsigned entrustTarget = 0;       // that ally's server id
    unsigned entrustCastMs = 0;
    unsigned entrustEndMs = 0;        // its end (compute_geo : the same duration the Timers ally row was given) ; 0 = not known yet
};

// A luopan seen this long after our Geo- cast is that cast's ; a cast whose luopan never came is forgotten after it.
static const unsigned GEO_SPAWN_WAIT_MS = 10000;
// a luopan missing from the entity list this long is gone (a shorter miss is a read that failed, not Full Circle)
static const unsigned GEO_GONE_MS = 1000;
// BG-Wiki "Luopan" : "Luopans have a maximum duration of 10 minutes."
static const int LUOPAN_MAX_SEC = 600;

void            geo_set_me(unsigned meId);                // the local player's server id, once a frame
void            geo_on_packet(int id, const unsigned char* b);   // 0x028 cat 4 by us (Geo- names the bubble, Indi- on an ally = Entrust), 0x00B zone-out
void            geo_frame(unsigned luopanId, int hpp, unsigned nowMs);   // once a frame : 0 = no luopan out
const GeoTrack& geo_track();
void            geo_entrust_set_end(unsigned endMs);       // compute_geo, once it found the duration
void            geo_entrust_clear();
void            geo_reset();                              // tests, and a character change
int             geo_bubble_left_sec(unsigned nowMs);      // the slope's figure, capped by the 10-minute life when the age is known ; -1 = not yet

// Cardinal Chant (BG-Wiki "Cardinal_Chant", read 2026-10-29) : a GEO's elemental magic gains a bonus by where the
// TARGET stands from the Geomancer when the spell lands -- "if the target is located to the East of the casting
// Geomancer" : East Magic Attack, South Magic Accuracy, West Magic Burst, North Magic Critical Hit rate ; the
// character's facing does not count. Between two cardinal points the two bonuses share, "9 steps in this tradeoff",
// true Northeast half of each : the 90 degrees in 8 steps of 11.25, the nearest step taken. Four ranks (GEO 25 / 45
// / 65 / 85), a -ra spell more than the others. The page gives the maximum at each point ; the share between two is
// shown rounded DOWN (the game's own rounding is not written there). North is the world's (the map's, +Z) : the
// in-game check of the direction is in docs/design/pet-module-spec.md §3.1 bis.
enum { CARD_N = 0, CARD_E, CARD_S, CARD_W };            // Magic Crit. / Magic Attack / Magic Accuracy / Magic Burst
struct CardinalBonus { int value[4] = {}; float share[4] = {}; };   // value : the bonus at that point now ; share 0..1
int           geo_cardinal_rank(int geoLevel);          // 0 (under 25) .. 4
CardinalBonus geo_cardinal(float deg, int rank, bool ra);   // deg : the target's direction from us, 0 N, 90 E, clockwise

void compute_geo(GameState& gs);   // game_mem.cpp

} // namespace aio
