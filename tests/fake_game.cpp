// fake_game.cpp -- see fake_game.h. The game's side of every seam the model and the Timers builder read.
#include "fake_game.h"
#include "check.h"
#include "model/party_state.h"
#include "model/gamestate.h"
#include "model/game_mem.h"
#include "model/model_io.h"
#include "model/model_clock.h"
#include "model/paths.h"
#include "model/selftest.h"
#include <windows.h>
#include <cstring>
#include <new>

using namespace aio;

// ---- the world ------------------------------------------------------------------------------------------------
namespace {

// The FFXiMain clock the model reads (party_state.cpp : CLK_PTR_RVA / CLK_SEC_OFF / CLK_OFF_RVA). These three numbers
// are a COPY of the model's, and a copy can drift -- which is why selfcheck() compares the tick the model actually
// computes with the one this file intends, and fails the suite when they disagree.
const u32 CLK_PTR_RVA = 0x492E10, CLK_SEC_OFF = 0x0C, CLK_OFF_RVA = 0x4E0AF8;

struct World {
    // memory, laid out as the game lays it out
    unsigned char party[18 * 0x7C];          // member array : slot 0 = you (self_party_base)
    unsigned char alliHdr[0x40];              // *party_ptr() -> alliance info ; +0x13 = active member count
    u32           partyPtr;                   // what party_ptr() points at (holds &alliHdr)
    u32           entities[0x900];            // entity_array() : index -> entity struct
    unsigned char ent[18][0x200];             // one entity struct per party slot (+0x1D0 = spawn type)
    unsigned char clockStruct[0x10];          // *(FFXiMain + CLK_PTR_RVA) -> +0x0C = unix seconds
    // what the rest of the game_mem surface answers
    PlayerInfo    me;
    unsigned      zone;
    unsigned short buffs[32]; int nbuff; bool buffsOk;
    int           n;
};
World* W = 0;

// The model clock and the game tick, one monotonic source. Starts well away from zero so no subtraction in the model
// ever wraps on the first frames, as it never does in game.
unsigned g_ms = 50000000u;
const unsigned TICK_EPOCH_SEC = 1000000u;

// FFXiMain as a module : a reserved region large enough to hold the two RVAs the clock needs, committed on demand.
u32 g_ffxiMain = 0;

}  // namespace

namespace fake {

unsigned now_ms() { return g_ms; }
unsigned now_tick() { return (TICK_EPOCH_SEC + g_ms / 1000u) * 60u; }

static void write_clock() {
    const u32 sec = TICK_EPOCH_SEC + g_ms / 1000u;
    memcpy(W->clockStruct + CLK_SEC_OFF, &sec, 4);
    const u32 clk = (u32)(uintptr_t)W->clockStruct, off = 0;
    memcpy((void*)(uintptr_t)(g_ffxiMain + CLK_PTR_RVA), &clk, 4);
    memcpy((void*)(uintptr_t)(g_ffxiMain + CLK_OFF_RVA), &off, 4);
    model_clock_pin(g_ms, (time_t)(1789000000 + g_ms / 1000u));
}

void advance_ms(unsigned ms) { g_ms += ms; write_clock(); }

void world(std::initializer_list<Member> members) {
    if (!W) {
        W = (World*)VirtualAlloc(0, sizeof(World), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        g_ffxiMain = (u32)(uintptr_t)VirtualAlloc(0, 0x500000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    memset(W, 0, sizeof(World));
    W->zone = 230;   // Southern San d'Oria : a town, no zone tracker mode
    W->buffsOk = true;
    W->partyPtr = (u32)(uintptr_t)W->alliHdr;
    int i = 0;
    for (const Member& m : members) {
        if (i >= 6) break;
        unsigned char* b = W->party + i * 0x7C;
        const int nl = (int)strlen(m.name);
        memcpy(b + 0x0A, m.name, nl < 17 ? nl : 17);
        memcpy(b + 0x1C, &m.id, 4);
        const unsigned short idx = (unsigned short)(0x400 + i);
        memcpy(b + 0x20, &idx, 2);
        const u32 hp = 1000, mp = 500, tp = 0, pk = 100u | (100u << 8) | (W->zone << 16);
        memcpy(b + 0x28, &hp, 4); memcpy(b + 0x2C, &mp, 4); memcpy(b + 0x30, &tp, 4); memcpy(b + 0x34, &pk, 4);
        b[0x71] = (unsigned char)(m.trust ? 0 : m.mjob); b[0x72] = 99; b[0x73] = 0; b[0x74] = 49;
        W->ent[i][0x1D0] = m.trust ? 0x0E : 0x0D;   // measured in game : players 0x0D, trusts 0x0E (party_state_roster.cpp)
        W->entities[idx] = (u32)(uintptr_t)W->ent[i];
        if (i == 0) {
            W->me.id = m.id; lstrcpynA(W->me.name, m.name, sizeof(W->me.name));
            W->me.hp = 1000; W->me.mp = 500; W->me.hpp = 100; W->me.mpp = 100;
            W->me.mjob = m.mjob; W->me.mlvl = 99; W->me.slvl = 49;
        }
        ++i;
    }
    W->n = i;
    W->alliHdr[0x13] = (unsigned char)i;

    // No case inherits another's saved model state : the per-character caches (data\cache\*_<id>.bin) are what a
    // fresh PartyState reads back on its first roster refresh -- casters, song tags, the self-cast ring.
    { char pat[MAX_PATH]; plugin_path(pat, sizeof(pat), "data\\cache\\*.bin");
      WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
      if (h != INVALID_HANDLE_VALUE) {
          do { char f[MAX_PATH]; char rel[MAX_PATH]; _snprintf(rel, sizeof(rel), "data\\cache\\%s", fd.cFileName); rel[sizeof(rel) - 1] = 0;
               plugin_path(f, sizeof(f), rel); DeleteFileA(f); } while (FindNextFileA(h, &fd));
          FindClose(h);
      } }
    // A new model, as after a fresh load. The builder's focus monitor is wiped the documented way (timers_reset).
    PartyState& ps = party();
    ps.~PartyState();
    new (&ps) PartyState();
    timers_reset();
    advance_ms(1);   // a fresh instant : ffxi_now_tick caches per model ms
}

void self_buffs(std::initializer_list<unsigned short> ids) {
    W->nbuff = 0; W->buffsOk = true;
    for (unsigned short s : ids) if (W->nbuff < 32) W->buffs[W->nbuff++] = s;
}
void self_buffs_unreadable() { W->nbuff = 0; W->buffsOk = false; }

void packet(int id, const unsigned char* bytes) {
    model_event_begin('P');
    model_feed_packet(id, bytes);
    model_event_end();
}

void zone_to(unsigned zone, unsigned loadMs) {
    unsigned char hdr[8] = { 0 };
    hdr[0] = 0x0B; hdr[1] = (unsigned char)(2 << 1);   // 0x00B, 8 bytes : no field is read, the event is the packet
    packet(0x00B, hdr);
    for (unsigned t = 0; t < loadMs; t += 500) { advance_ms(500); frame(); }
    W->zone = zone;
    for (int i = 0; i < W->n; ++i) { unsigned char* b = W->party + i * 0x7C; const u32 pk = 100u | (100u << 8) | (zone << 16); memcpy(b + 0x34, &pk, 4); }
    hdr[0] = 0x0A; hdr[1] = (unsigned char)(2 << 1);
    packet(0x00A, hdr);
}

void frame() {
    FrameInput in;
    in.meId = W->me.id;
    in.zone = W->zone;
    model_event_begin('F');
    model_frame_upkeep(in);
    model_event_end();
}

bool build(TimersRows& out) {
    static GameState gs;   // large ; one instance, refilled per build like the poller refills the frame snapshot
    gs = GameState();
    gs.inGame = true;
    gs.me = W->me;
    gs.zone = W->zone;
    gs.buffsOk = W->buffsOk;
    gs.nbuff = W->nbuff;
    for (int i = 0; i < W->nbuff; ++i) gs.buffs[i] = W->buffs[i];
    model_event_begin('T');
    const bool built = timers_build_rows(&gs, false, false, out);
    model_event_end();
    return built;
}

bool step(TimersRows& out) { frame(); return build(out); }

void settle() {
    static TimersRows scratch;
    for (int i = 0; i < 10; ++i) { step(scratch); advance_ms(1000); }
    step(scratch);
}

int find_row(const TimersRows& r, int icon, const char* who) {
    for (int i = 0; i < r.nb; ++i) {
        if (r.bufs[i].icon != icon) continue;
        const bool same = who ? (r.bufs[i].who && strcmp(r.bufs[i].who, who) == 0) : (r.bufs[i].who == 0);
        if (same) return i;
    }
    return -1;
}
int count_rows(const TimersRows& r, int icon) {
    int c = 0; for (int i = 0; i < r.nb; ++i) if (r.bufs[i].icon == icon) ++c; return c;
}

void dump(const TimersRows& r, const char* label) {
    printf("        rows [%s] : %d duration, %d recast\n", label, r.nb, r.nr);
    for (int i = 0; i < r.nb; ++i) {
        const TimersRow& x = r.bufs[i];
        printf("          %2d. icon=%-4d src=%d order=%-3d rem=%-11d %s%s%s%s%s mark=%d\n", i, x.icon, x.src, x.order, x.rem,
               x.who ? x.who : "", x.who ? " - " : "", x.name ? x.name : "(no name)", x.tag ? x.tag : "", x.post ? x.post : "", x.mark);
    }
}

int selfcheck() {
    const int before = g_fail;
    SECTION("fake game : it reaches the model (otherwise every Timers test below is vacuous)");
    world({ { 0x00010001u, "Tetsouo", 7, false }, { 0x00010002u, "Kaories", 5, false }, { 0x00010003u, "Monberaux", 0, true } });
    frame();
    CHECK_EQ(party().self_id(), 0x00010001u);
    CHECK_EQ(party().count, 3);
    CHECK_STR(party().pc_name_by_id(0x00010002u), "Kaories");
    CHECK(party().is_trust(0x00010003u));
    CHECK(!party().is_trust(0x00010002u));
    CHECK_EQ(party().self_main_job(), 7);
    CHECK_EQ(ffxi_now_tick(), now_tick());   // the clock RVAs copied above still match the model's
    advance_ms(3000);
    CHECK_EQ(ffxi_now_tick(), now_tick());
    return g_fail - before;
}

}  // namespace fake

// ---- the game_mem surface the linked model calls ---------------------------------------------------------------
// Values the fake world defines are served ; everything else FAILS (0 / false), which is what the model must already
// survive in game (a not-ready read). Nothing here decides anything on the model's behalf.
namespace aio {

bool model_read_u32(u32 addr, u32* out) { u32 v = 0; const bool ok = windower::safe_read(addr, &v); if (out) *out = ok ? v : 0; return ok; }
bool model_copy(u32 addr, void* out, unsigned n) {
    bool ok = false;
    __try { memcpy(out, (const void*)(uintptr_t)addr, n); ok = true; } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
u32 model_module_base(const char* name) { return (name && lstrcmpiA(name, "FFXiMain.dll") == 0) ? g_ffxiMain : 0; }
bool tape_recording() { return false; }
void tape_note(uint16_t, u32, u32, int, const void*, unsigned) {}
void tape_event_begin(char, unsigned, long long) {}
void tape_event_end() {}
void tape_packet(int, const unsigned char*) {}
void tape_frame(const void*, unsigned) {}

u32 data_root()                      { return 0; }
u32 entity_array()                   { return W ? (u32)(uintptr_t)W->entities : 0; }
u32 party_ptr()                      { return W ? (u32)(uintptr_t)&W->partyPtr : 0; }
u32 key_items_base()                 { return 0; }
u32 self_party_base(unsigned selfId) { return (W && selfId && selfId == W->me.id) ? (u32)(uintptr_t)W->party : 0; }
unsigned zone_id()                   { return W ? W->zone : 0; }
unsigned count_item(unsigned)        { return 0; }
unsigned entity_id_by_index(unsigned index) {
    if (!W || index >= 0x900 || !W->entities[index]) return 0;
    for (int i = 0; i < W->n; ++i) if (W->entities[index] == (u32)(uintptr_t)W->ent[i]) { u32 id = 0; memcpy(&id, W->party + i * 0x7C + 0x1C, 4); return id; }
    return 0;
}
bool owns_key_item(unsigned)         { return false; }
bool refresh_items()                 { return false; }
int  read_jp_gift_rank(unsigned)     { return 0; }
int  read_jp_u8(unsigned)            { return 0; }
int  read_merit_level(unsigned)      { return 0; }
int  count_items(const unsigned*, int, unsigned*) { return 0; }
bool entity_name_by_index(unsigned, char* out, int sz) { if (out && sz > 0) out[0] = 0; return false; }
bool entity_pos_verified(unsigned, unsigned, float&, float&, float&, bool* despawned) { if (despawned) *despawned = false; return false; }
bool read_capacity_points(unsigned, unsigned&, unsigned&) { return false; }
int  read_entities_by_id(const unsigned*, int, EntityVitals*) { return 0; }
bool read_equipment_ext(unsigned short ids[16], unsigned char ext[16][24]) { memset(ids, 0, 32); memset(ext, 0, 16 * 24); return false; }
bool read_player(PlayerInfo& o)      { if (!W || !W->me.id) return false; o = W->me; return true; }
int  read_player_buffs(unsigned short* out, int maxN, bool* ok) {
    if (ok) *ok = W && W->buffsOk;
    if (!W || !W->buffsOk || !out) return 0;
    int n = W->nbuff < maxN ? W->nbuff : maxN;
    for (int i = 0; i < n; ++i) out[i] = W->buffs[i];
    return n;
}
bool read_pointwatch(PwMem&)         { return false; }
bool read_treasure_pool(TreasureSlot[10]) { return false; }

// plugin_dir / plugin_path : tests/t_config.cpp (a scratch folder per test process). world() empties its model
// caches, so no case inherits the roster, casters or song tags another case saved there.

// Diagnostics the model reports into. Their return values never reach a decision (checked for the replay, 2026-09-13).
void selftest_add(const char*, CheckFn) {}
bool watch_enabled() { return false; }   // //aio watch : the flip/cap watchers observe the rows, they never shape them
void dec_record(const char*, const char*, ...) {}
void sentinel_packet_buffs(const unsigned short*, int) {}
void sentinel_packet_member(unsigned, const char*, unsigned, unsigned) {}
void fm_pw_expect(unsigned, unsigned, unsigned, unsigned, unsigned) {}
void fm_pw_merit_expect(unsigned, unsigned, unsigned) {}

}  // namespace aio
