// geo_state.cpp -- the Geomancer module's packet side (model_feed_packet -> geo_on_packet) and the state kept between
// frames : which Geo- the luopan carries, the slope of its HP, and the Indi- we entrusted. SPEC
// docs/design/geo-module-spec.md.
//
// The bubble's NAME comes from our own cast, not from the luopan : the pet packets (0x068) and the luopan's entity
// carry a name ("Luopan") and HP, never the spell. A GEO holds one luopan at a time and cannot cast a Geo- while one
// is out, so "our last Geo- that finished" (0x028 category 4, actor = us, param = the spell) IS the bubble's spell
// from the moment its luopan appears until it disappears (Full Circle, its HP gone, the 10-minute cap, a zone).
// Category 4 is the FINISH : an interrupted cast is category 8, so a spell that never went off names nothing.
#include "model/geo_state.h"
#include "model/party_state.h"            // model_decode_action
#include "model/geo_spells_gen.h"
#include "model/model_clock.h"
#include <math.h>                 // geo_cardinal : fmodf
#include <string.h>

namespace aio {

static unsigned g_meId = 0;
static GeoTrack g_geo;
static unsigned g_goneMs = 0;    // the first frame our luopan was missing ; 0 = it is there
static unsigned g_staleId = 0;   // the luopan that was out when our last Geo- went off : never the new one's

void            geo_set_me(unsigned meId) { g_meId = meId; }
const GeoTrack& geo_track() { return g_geo; }
void            geo_entrust_set_end(unsigned endMs) { g_geo.entrustEndMs = endMs ? endMs : 1u; }
void            geo_entrust_clear() { g_geo.entrustSpell = 0; g_geo.entrustTarget = 0; g_geo.entrustCastMs = 0; g_geo.entrustEndMs = 0; }
void            geo_reset() { g_geo = GeoTrack{}; g_staleId = 0; g_goneMs = 0; }

// BG-Wiki "Cardinal_Chant" (read 2026-10-29) : the maximum at each point, rank I..IV, [N E S W] = Magic Crit., Magic
// Attack, Magic Accuracy, Magic Burst ; a spell that is not a -ra, then a -ra
static const int CARD_MAX[4][2][4] = {
    { { 5, 5, 5, 10 },  { 8, 8, 8, 15 } },
    { { 7, 7, 7, 14 },  { 10, 10, 10, 19 } },
    { { 9, 10, 10, 18 }, { 13, 14, 14, 24 } },
    { { 11, 13, 13, 22 }, { 16, 17, 17, 28 } },
};

int geo_cardinal_rank(int lvl) { return lvl >= 85 ? 4 : lvl >= 65 ? 3 : lvl >= 45 ? 2 : lvl >= 25 ? 1 : 0; }

CardinalBonus geo_cardinal(float deg, int rank, bool ra) {
    CardinalBonus b;
    if (!(deg > -1e6f && deg < 1e6f)) deg = 0.0f;   // NaN / inf / absurd : never a loop, never a wrong lane
    deg = fmodf(deg, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    if (deg >= 360.0f) deg = 0.0f;
    const int from = (int)(deg / 90.0f) % 4, to = (from + 1) % 4;
    int step = (int)((deg - 90.0f * (float)from) / 11.25f + 0.5f);   // 0 .. 8
    if (step > 8) step = 8;
    b.share[to] = (float)step / 8.0f; b.share[from] = 1.0f - b.share[to];
    if (rank < 1 || rank > 4) return b;
    for (int k = 0; k < 4; ++k) b.value[k] = (int)((float)CARD_MAX[rank - 1][ra ? 1 : 0][k] * b.share[k]);
    return b;
}

static void forget_bubble() {
    g_goneMs = 0;
    g_geo.bubbleSpell = 0; g_geo.bubbleCastMs = 0; g_geo.luopanId = 0; g_geo.spawnMs = 0; g_geo.spawnKnown = false;
    g_geo.slope.reset();
}

static void on_action(const unsigned char* p, unsigned nowMs) {
    ActionDecode a;
    if (!model_decode_action(p, a)) return;
    if (a.category != 4 || !g_meId || a.actor != g_meId) return;   // a spell WE finished casting
    const GeoSpellRow* s = geo_spell(a.param);
    if (!s) return;
    if (s->geo) {
        // a new Geo- : the luopan it makes is not the one we may still see this frame (the poll can lag the packet by
        // a frame) -- that one is remembered as stale and never adopted -- so the next luopan seen is taken as new
        // (geo_frame) and the measurement starts over
        g_staleId = g_geo.luopanId;
        g_geo.bubbleSpell = s->id; g_geo.bubbleCastMs = nowMs ? nowMs : 1u;
        g_geo.luopanId = 0; g_geo.spawnMs = 0; g_geo.spawnKnown = false; g_geo.slope.reset();
        return;
    }
    // an Indi- that landed on ANOTHER player is an entrusted one (an Indi- always targets its caster otherwise) ; the
    // party side keeps the matching ally row with its duration (party_state.cpp act_ally_buffs, geo_dur.h)
    const unsigned tid = a.n ? a.ids[0] : 0;
    if (!tid || tid == g_meId) return;
    g_geo.entrustSpell = s->id; g_geo.entrustTarget = tid; g_geo.entrustCastMs = nowMs ? nowMs : 1u; g_geo.entrustEndMs = 0;
}

void geo_on_packet(int id, const unsigned char* b) {
    if (id == 0x028) on_action(b, model_now_ms());
    else if (id == 0x00B) forget_bubble();             // zone-out : the luopan does not follow
}

// A luopan that was seen and is gone (or replaced by another entity) ends its bubble ; a Geo- cast whose luopan does
// not come within GEO_SPAWN_WAIT_MS is forgotten. A luopan seen with no cast of ours behind it (the plugin loaded with
// one out) is measured all the same, nameless and without the 10-minute cap.
void geo_frame(unsigned luopanId, int hpp, unsigned nowMs) {
    // the luopan our last cast replaced, seen once more by a poll that lags the packet : not ours. Only for a moment --
    // an entity id can be handed out again, and the new luopan must not stay invisible for that
    if (luopanId && luopanId == g_staleId && nowMs - g_geo.bubbleCastMs < 1500u) luopanId = 0;
    else if (luopanId) g_staleId = 0;
    if (!luopanId) {
        // gone for GEO_GONE_MS, not for one frame : a single missed entity read must not erase a name only a cast can
        // give back (CLAUDE.md rule 10)
        if (g_geo.luopanId) { if (!g_goneMs) g_goneMs = nowMs | 1u; else if (nowMs - g_goneMs >= GEO_GONE_MS) { forget_bubble(); g_goneMs = 0; } }
        else if (g_geo.bubbleSpell && nowMs - g_geo.bubbleCastMs > GEO_SPAWN_WAIT_MS) forget_bubble();
        return;
    }
    g_goneMs = 0;
    if (luopanId != g_geo.luopanId) {
        // another entity than the one this bubble was measured on, with no cast of ours in between (a cast resets
        // luopanId) : the old luopan is gone and its spell with it -- this one is measured nameless
        if (g_geo.luopanId) { g_geo.bubbleSpell = 0; g_geo.bubbleCastMs = 0; }
        g_geo.luopanId = luopanId; g_geo.spawnMs = nowMs; g_geo.slope.reset();
        g_geo.spawnKnown = g_geo.bubbleSpell && nowMs - g_geo.bubbleCastMs <= GEO_SPAWN_WAIT_MS;
    }
    g_geo.slope.sample(hpp, nowMs);
}

int geo_bubble_left_sec(unsigned nowMs) {
    int left = g_geo.slope.remaining_sec(nowMs);
    if (left >= 0 && g_geo.spawnKnown) {
        const int cap = LUOPAN_MAX_SEC - (int)((nowMs - g_geo.spawnMs) / 1000u);
        if (cap < left) left = cap < 0 ? 0 : cap;
    }
    return left;
}

} // namespace aio
