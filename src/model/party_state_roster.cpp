// party_state_roster.cpp -- the party/alliance ROSTER, split out of party_state.cpp. PURE MOVE :
// the packet-fed roster (on_dd 0x0DD / on_df 0x0DF + find + save/load disk cache) AND the live
// memory roster (load_from_memory + read_member / entity_xz), the job-abbreviation + trust-job
// tables, and the DEMO/SIM harness (//aio party N). The g_party singleton stays in party_state.cpp.
#include "model/party_state.h"
#include "model/party_state_internal.h"   // pkt_u16 / pkt_u32 / pkt_bytes (shared packet readers)
#include "model/game_mem.h"               // party_ptr / self_party_base / entity_array / read_player / read_member helpers
#include "model/paths.h"                  // plugin_path (the roster cache path)
#include "windower.h"                      // safe_read / valid_ptr (guarded game-memory reads)
#include "windower_debug.h"                // debug::log (//aio bcaptlog : per-member party dump)
#include <windows.h>                       // CreateFileA / ReadFile / WriteFile
#include <string.h>                        // memcpy
#include <math.h>                          // sqrtf
#include <stdio.h>                         // _snprintf (per-character cache filename)

namespace aio {

using windower::safe_read;
using windower::valid_ptr;

static int g_demoLevel = 0;
int  party_demo_level() { return g_demoLevel; }
void set_party_demo_level(int level) { g_demoLevel = level < 0 ? 0 : (level > 3 ? 3 : level); }

static int g_demoCount = 6;                                                  // party (tier 0) demo member count (1..6)
int  party_demo_count() { return g_demoCount; }
void set_party_demo_count(int c) { g_demoCount = c < 1 ? 1 : (c > 6 ? 6 : c); }

static int g_demoAlly[2] = { 6, 6 };                                         // demo member count for alliance tier 1 / 2 (1..6)
int  party_demo_alliance_count(int tier) { return tier == 1 ? g_demoAlly[0] : tier == 2 ? g_demoAlly[1] : 0; }

// //aio party N : ONE total count (0..18) -> derive level + per-box counts. You (the player) are
// always member #1, so the party box fills first (up to 6), then alliance 1, then alliance 2.
void set_party_demo_total(int total) {
    if (total < 0) total = 0; if (total > 18) total = 18;
    if (total == 0) { g_demoLevel = 0; return; }                            // 0 -> back to live data
    const int p  = total > 6  ? 6 : total;                                  // party      : 1..6
    const int a1 = total > 12 ? 6 : total > 6 ? total - 6 : 0;              // alliance 1 : 0..6
    const int a2 = total > 12 ? total - 12 : 0;                             // alliance 2 : 0..6
    g_demoCount   = p;
    g_demoAlly[0] = a1 < 1 ? 1 : a1;                                        // a shown tier always has >=1 row
    g_demoAlly[1] = a2 < 1 ? 1 : a2;
    g_demoLevel   = total <= 6 ? 1 : total <= 12 ? 2 : 3;                   // party / +alliance1 / +alliance2
}

static bool g_demoSelect = false;
bool demo_select()            { return g_demoSelect; }
void set_demo_select(bool on) { g_demoSelect = on; }

static int g_simExtra = 0;                                                   // fake members appended to the LIVE party (test the layout reacting to size)
int  party_sim_extra() { return g_simExtra; }
void set_party_sim_extra(int n) { g_simExtra = n < 0 ? 0 : (n > 5 ? 5 : n); }

// PER-CHARACTER roster cache. This was a single "party.bin" for the whole install, while its contents (your
// party's names, jobs, HP) are strictly per character -- so at login, and INDEFINITELY at the character-select
// screen where read_player fails and load_from_memory bails early, another character's full roster was drawn on
// your screen. Not cached in a static: one client can log out and back in as someone else.
// Returns false when no character is resolved yet -> the caller skips the read/write entirely rather than
// falling back to a shared name (that fallback is how cross-contamination creeps back in).
static bool cache_path_for(unsigned selfId, char* out, int cap) {
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (!selfId) return false;
    char rel[64]; _snprintf(rel, sizeof(rel), "data\\cache\\party_%08X.bin", selfId); rel[sizeof(rel) - 1] = 0;
    plugin_path(out, cap, rel);
    return out[0] != 0;
}

static const char* JOBS[] = {
    "", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD",
    "RNG", "SAM", "NIN", "DRG", "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN",
    "SPC"   // 23 : internal sentinel for special trusts (chemist/beast/etc.) -> XivParty's spc.png
};
const char* job_abbr(int id) { return (id >= 0 && id <= 23) ? JOBS[id] : ""; }

static bool streq(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int  job_id(const char* a) { if (!a || !a[0]) return 0; for (int i = 1; i <= 23; ++i) if (streq(JOBS[i], a)) return i; return 0; }
int job_id_from_abbr(const char* a) { return job_id(a); }   // public wrapper for the job-icon atlas lookup

// Trust -> job table, ported from XivParty (jobs.lua jobs.trusts). The party member array
// holds NO job for trusts, so we resolve it by internal name (matches the in-game name field,
// e.g. 'Joachim','Monberaux'). Name-only lookup (first match wins) -- the few I/II variants
// that differ would need the entity model id to disambiguate; not worth a second pointer hop.
struct TrustJob { const char* name; const char* mj; const char* sj; };
static const TrustJob TRUSTS[] = {
    {"Amchuchu","RUN","WAR"},{"ArkEV","PLD","WHM"},{"ArkHM","WAR","NIN"},{"August","PLD","WAR"},
    {"Curilla","PLD",0},{"Gessho","NIN","WAR"},{"Mnejing","PLD","WAR"},{"Rahal","PLD","WAR"},
    {"Rughadjeen","PLD",0},{"Trion","PLD","WAR"},{"Valaineral","PLD","WAR"},
    {"Abenzio","THF","WAR"},{"Abquhbah","WAR",0},{"Aldo","THF",0},{"Areuhat","WAR",0},
    {"ArkGK","SAM","DRG"},{"ArkMR","BST","THF"},{"Ayame","SAM",0},{"BabbanMheillea","MNK",0},
    {"Balamor","DRK",0},{"Chacharoon","THF",0},{"Cid","WAR",0},{"Darrcuiln","SPC",0},
    {"Excenmille","PLD",0},{"Fablinix","RDM","BLM"},{"Gilgamesh","SAM",0},{"Halver","PLD","WAR"},
    {"Ingrid","WAR","WHM"},{"Iroha","SAM",0},{"IronEater","WAR",0},{"Klara","WAR",0},
    {"LehkoHabhoka","THF","BLM"},{"LheLhangavo","MNK",0},{"LhuMhakaracca","BST","WAR"},
    {"Lilisette","DNC",0},{"Lion","THF",0},{"Luzaf","COR","NIN"},{"Maat","MNK",0},
    {"Maximilian","WAR","THF"},{"Mayakov","DNC",0},{"Mildaurion","PLD","WAR"},{"Morimar","BST",0},
    {"Mumor","DNC","WAR"},{"NajaSalaheem","MNK","WAR"},{"Naji","WAR",0},{"NanaaMihgo","THF",0},
    {"Nashmeira","PUP","WHM"},{"Noillurie","SAM","PLD"},{"Prishe","MNK","WHM"},{"Rainemard","RDM",0},
    {"RomaaMihgo","THF",0},{"Rongelouts","WAR",0},{"Selh'teus","SPC",0},{"ShikareeZ","DRG","WHM"},
    {"Tenzen","SAM",0},{"Teodor","SAM","BLM"},{"UkaTotlihn","DNC","WAR"},{"Volker","WAR",0},
    {"Zazarg","MNK",0},{"Zeid","DRK",0},{"Matsui-P","NIN","BLM"},
    {"Elivira","RNG","WAR"},{"Makki-Chebukki","RNG",0},{"Margret","RNG",0},{"Najelith","RNG",0},
    {"SemihLafihna","RNG",0},
    {"Adelheid","SCH",0},{"Ajido-Marujido","BLM","RDM"},{"ArkTT","BLM","DRK"},{"D.Shantotto","BLM",0},
    {"Gadalar","BLM",0},{"Kayeel-Payeel","BLM",0},{"Kukki-Chebukki","BLM",0},{"Leonoyne","BLM",0},
    {"Ovjang","RDM","WHM"},{"Robel-Akbel","BLM",0},{"Rosulatia","BLM",0},{"Shantotto","BLM",0},
    {"Ullegore","BLM",0},
    {"Cherukiki","WHM",0},{"FerreousCoffin","WHM","WAR"},{"Karaha-Baruha","WHM","SMN"},{"Kupipi","WHM",0},
    {"MihliAliapoh","WHM",0},{"Monberaux","SPC",0},{"Ygnas","WHM",0},
    {"Arciela","RDM",0},{"Joachim","BRD","WHM"},{"KingOfHearts","RDM","WHM"},{"Koru-Moru","RDM",0},
    {"Qultada","COR",0},{"Ulmia","BRD",0},
    {"Brygid","GEO",0},{"Cornelia","GEO",0},{"Kupofried","GEO",0},{"KuyinHathdenna","GEO",0},
    {"Moogle","GEO",0},{"Sakura","GEO",0},{"StarSibyl","GEO",0},
    {"Apururu","WHM","RDM"},{"Flaviria","DRG","WAR"},{"InvincibleShield","WAR","MNK"},
    {"JakohWahcondalo","THF","WAR"},{"Pieuje","WHM",0},{"Sylvie","GEO","WHM"},{"Yoran-Oran","WHM",0},
};
// resolve a trust's main/sub job by name -> true if found (mj/sj are job ids, 0 if none/SPC).
static bool trust_job(const char* name, int& mj, int& sj) {
    if (!name || !name[0]) return false;
    for (unsigned i = 0; i < sizeof(TRUSTS) / sizeof(TRUSTS[0]); ++i)
        if (streq(name, TRUSTS[i].name)) { mj = job_id(TRUSTS[i].mj); sj = job_id(TRUSTS[i].sj); return true; }
    return false;
}

int PartyState::find(unsigned id) const {
    for (int i = 0; i < count; ++i) if (m[i].id == id) return i;
    return -1;
}

void PartyState::on_dd(const unsigned char* p) {
    if (pkt_bytes(p) < 0x3B) return;                       // reads name up to p[0x3A]
    unsigned id = pkt_u32(p, 0x04);
    if (!id) return;
    int i = find(id);
    bool added = false;
    if (i < 0) { if (count >= 6) return; i = count++; m[i] = PMember(); m[i].id = id; added = true; }
    m[i].hp    = (int)pkt_u32(p, 0x08);
    m[i].mp    = (int)pkt_u32(p, 0x0C);
    m[i].tp    = (int)pkt_u32(p, 0x10);
    m[i].flags = pkt_u32(p, 0x14);
    m[i].mpp = p[0x1D];
    m[i].hpp = p[0x1E];
    m[i].maxHp = m[i].hpp > 0 ? m[i].hp * 100 / m[i].hpp : m[i].hp;   // derive max -> 0x0DF can refresh %
    m[i].maxMp = m[i].mpp > 0 ? m[i].mp * 100 / m[i].mpp : m[i].mp;
    m[i].mjob = p[0x22];
    m[i].mlvl = p[0x23];   // main-job level (between main @0x22 and sub @0x24)
    m[i].sjob = p[0x24];
    m[i].slvl = p[0x25];   // sub-job level (by symmetry, right after sub @0x24 ; matches the memory-block +0x74 finding)
    m[i].zone = (int)p[0x20] | ((int)p[0x21] << 8);   // member zone id (set when out of our zone)
    int j = 0; for (; j < 19 && p[0x28 + j]; ++j) m[i].name[j] = (char)p[0x28 + j];
    m[i].name[j] = 0;
    if (added) save();   // only when the roster SET actually grows (name/jobs are set in this same call) --
                         // NOT on every vitals-carrying 0x0DD (was a full disk write per packet, packet thread)
}

void PartyState::on_df(const unsigned char* p) {
    if (pkt_bytes(p) < 0x14) return;          // reads tp @0x10..0x13
    int i = find(pkt_u32(p, 0x04));
    if (i < 0) return;                        // only vitals for a member we already know
    m[i].hp = (int)pkt_u32(p, 0x08);
    m[i].mp = (int)pkt_u32(p, 0x0C);
    m[i].tp = (int)pkt_u32(p, 0x10);
    if (m[i].hp > m[i].maxHp) m[i].maxHp = m[i].hp;   // track observed max (gear/buffs raise it)
    if (m[i].mp > m[i].maxMp) m[i].maxMp = m[i].mp;
    if (m[i].maxHp > 0) m[i].hpp = m[i].hp * 100 / m[i].maxHp;   // bar fill follows the value
    if (m[i].maxMp > 0) m[i].mpp = m[i].mp * 100 / m[i].maxMp;
}

// version tag tied to the struct size -> any PMember layout change auto-invalidates old caches.
// THE single reset point for a character switch on one client. See the declaration in party_state.h for why this
// must stay the only one. Called every frame from the poller; a no-op unless the id actually changed.
void PartyState::on_character_changed(unsigned newId) {
    static unsigned s_last = 0;
    if (!newId || newId == s_last) { if (newId) s_last = newId; return; }
    const bool wasSomeoneElse = (s_last != 0);
    s_last = newId;
    if (!wasSomeoneElse) return;                    // first login of this process : nothing stale to clear

    // Derived state keyed to "who I am". None of it self-heals: it is built from packets we watched as the
    // PREVIOUS character, so it would render their roll pips / song tags / ally buffs under the new one.
    for (int i = 0; i < 1024; ++i) { buffCaster_[i] = 0; rollVal_[i] = 0; rollLuck_[i] = 0; songMod_[i] = 0; selfBuffSpell_[i] = 0; }
    for (int i = 0; i < 64; ++i) selfCasts_[i] = SelfCast{};
    selfCastHead_ = 0;
    otherBuffN_ = 0;
    jobShadowN_ = 0;                                // else the 24 slots fill with two characters' alliances and new members stop being tracked
    buffTimerN_ = 0;                                // refilled by the 0x063 order-9 full refresh at login
    selfGeo_ = GeoAura{};
    for (int i = 0; i < 256; ++i) learnedMs_[i] = 0;
    pw_.xpReg = RateReg{}; pw_.cpReg = RateReg{}; pw_.epReg = RateReg{};   // else the new character's X/h is blended with the old one's samples

    // The roster cache belongs to the previous character too : drop it and re-arm the load for the new one.
    count = 0;
    cacheLoaded_ = false; cacheChar_ = 0;

    // Zone tracker + Limbus coffers, and their per-character files.
    zt_on_character_changed();
}

static const int CACHE_VER = (int)(0xA10D0000u | (sizeof(PMember) & 0xFFFF));

void PartyState::save() const {
    char path[300]; if (!cache_path_for(selfId_, path, sizeof(path))) return;   // no character -> never write a shared file
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; int ver = CACHE_VER;
    WriteFile(h, &ver, sizeof(ver), &w, 0);
    WriteFile(h, &count, sizeof(count), &w, 0);
    WriteFile(h, m, sizeof(PMember) * count, &w, 0);
    CloseHandle(h);
}

void PartyState::load() {
    // Needs the character, so this can no longer run from aio_plugin_init (nothing is logged in yet) : it is
    // driven from the poller once read_player succeeds. Before, the cache was loaded blind at init -- which is
    // exactly what put another character's roster on screen at the character-select screen.
    PlayerInfo me;
    char path[300];
    if (!read_player(me) || !cache_path_for(me.id, path, sizeof(path))) return;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    int ver = 0, c = 0; DWORD got = 0;
    if (ReadFile(h, &ver, sizeof(ver), &got, 0) && got == sizeof(ver) && ver == CACHE_VER &&
        ReadFile(h, &c, sizeof(c), &got, 0) && got == sizeof(c) && c > 0 && c <= 6) {
        if (ReadFile(h, m, sizeof(PMember) * c, &got, 0) && got == sizeof(PMember) * (DWORD)c) count = c;
    }
    CloseHandle(h);
}

// LIVE roster from FFXI memory -> the party is correct the instant the plugin loads
// (no waiting for packets). Reversed against the Ashita SDK partymember_t and verified
// in-game 2026-06-26 (retail/Carbuncle). Layout (member stride 0x7C, 18 slots; party =
// slots 0..5, alliance party 2 = 6..11, alliance party 3 = 12..17 -- all VERIFIED in an
// alliance 2026-06-28). Anchor: g = *(LuaCore+0x1C8400); *(g+0x248) points 4 bytes INTO
// member[0] -> base = that - 4 (self-validated against the player id).
// Member fields used: +0x0A name(18) +0x1C serverid +0x28 hp +0x2C mp +0x30 tp +0x34 hp%
// +0x35 mp% +0x36 zone(u16) +0x3C flags +0x71 main-job +0x72 main-lvl +0x73 sub-job.
// Read ONE member block (0x7C bytes) in a SINGLE SEH-guarded copy, then parse every field from
// the local buffer (no further guarded reads -> one SEH frame instead of ~25 per member). The
// SINGLE source of truth for the member-array offsets, shared by the party and alliance loops.
// Returns false on an unmapped block or an empty slot (id == 0).
// `ent` = entity (position-object) array (g+0x24) ; (px,pz) = the player's horizontal position.
// The member's entity index lives at member+0x20 ; ent[idx] is a position object with X @+0x04,
// Z @+0x0C (Y @+0x08 = height, ignored). dist = horizontal distance to the player (yalms).
// entity position : ent[idx] (the entity array from game_mem entity_array()) -> X @+0x04, Z @+0x0C.
// Returns true + fills x/z when the index is in range and the entity object is readable. Shared by a
// member's position (read_member) and the player's own (load_from_memory).
static bool entity_xz(u32 ent, u32 idx, float& x, float& z) {
    if (!ent || !idx || idx >= 0x900) return false;
    u32 p = 0;
    if (!safe_read(ent + idx * 4, &p) || !valid_ptr(p)) return false;
    u32 a = 0, c = 0; safe_read(p + 0x04, &a); safe_read(p + 0x0C, &c);
    x = *(float*)&a; z = *(float*)&c;
    return true;
}

// SpawnType byte of the entity at party-member entity index `idx` (ent[idx] + 0x1D0). It is a bitfield :
// bit 0x01 = PC (real player), bit 0x02 = NPC (trust / fellow), with context bits 0x0C also set here
// (verified in-game : players read 0x0D, trusts 0x0E). 0 = entity unreadable (caller -> name-DB fallback).
static unsigned char entity_spawn(u32 ent, u32 idx) {
    if (!ent || !idx || idx >= 0x900) return 0;
    u32 p = 0;
    if (!safe_read(ent + idx * 4, &p) || !valid_ptr(p)) return 0;
    u32 sp = 0;
    return safe_read(p + 0x1D0, &sp) ? (unsigned char)(sp & 0xFF) : 0;
}

static bool read_member(u32 mb, PMember& pm, u32 ent, float px, float pz) {
    unsigned char b[0x7C];
    __try { memcpy(b, (const void*)mb, sizeof(b)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    const u32 id = *(const u32*)(b + 0x1C);
    if (!id) return false;                                  // empty slot
    pm = PMember();
    u32 idx = *(const unsigned short*)(b + 0x20);           // entity index
    float mx = 0.0f, mz = 0.0f;
    if (entity_xz(ent, idx, mx, mz) && (mx != 0.0f || mz != 0.0f)) {
        float dx = mx - px, dz = mz - pz; pm.dist = sqrtf(dx * dx + dz * dz);
    }
    pm.id = id;
    int k = 0; for (; k < 18 && b[0x0A + k]; ++k) pm.name[k] = (char)b[0x0A + k];
    pm.name[k] = 0;
    pm.hp = (int)*(const u32*)(b + 0x28); pm.mp = (int)*(const u32*)(b + 0x2C); pm.tp = (int)*(const u32*)(b + 0x30);
    const u32 pk = *(const u32*)(b + 0x34);
    pm.hpp = (int)(pk & 0xFF); pm.mpp = (int)((pk >> 8) & 0xFF); pm.zone = (int)((pk >> 16) & 0xFFFF);
    pm.flags = *(const u32*)(b + 0x3C);
    pm.maxHp = pm.hpp > 0 ? pm.hp * 100 / pm.hpp : pm.hp;   // derive max -> 0x0DF can refresh %
    pm.maxMp = pm.mpp > 0 ? pm.mp * 100 / pm.mpp : pm.mp;
    const u32 jw = *(const u32*)(b + 0x70);                 // jobs: main job @+0x71, main lvl @+0x72, sub job @+0x73
    pm.mjob = (jw >> 8) & 0xFF; pm.sjob = (jw >> 24) & 0xFF; pm.mlvl = (jw >> 16) & 0xFF;
    pm.slvl = b[0x74];                                       // sub-job level @+0x74 (verified: self=slot0 matched the known value)
    // TRUST vs real player (Timers "Buff source" filter). AUTHORITATIVE : the entity SpawnType NPC bit
    // (0x02) -- trusts read it set, players carry the PC bit (0x01) instead. Independent of name/job.
    const unsigned char spawn = entity_spawn(ent, idx);
    const bool spawnKnown = (spawn != 0);
    if (spawnKnown) pm.isTrust = (spawn & 0x02) ? 1 : 0;
    // Trusts carry NO job in the packet -> resolve their main/sub job by name for display. A KNOWN trust name is
    // ALSO authoritative for isTrust : ANOTHER player's trust (you're partied with them) can read a SpawnType
    // WITHOUT the NPC bit in your client, so trusting the byte alone left is_trust() false for it -> the Timers
    // "Buff source : me + players" filter then failed to hide, e.g., their Monberaux's cures. A job-less member
    // whose name is in the global TRUSTS[] table IS a trust (a real player always carries a job), so mark it one
    // regardless of what the entity byte said. (Was: only when the entity was unreadable -- too narrow.)
    if (pm.mjob == 0) { int mj = 0, sj = 0; if (trust_job(pm.name, mj, sj)) { pm.mjob = mj; pm.sjob = sj; pm.isTrust = 1; } }
    return true;
}

void PartyState::load_from_memory() {
    u32 pp = party_ptr();                                 // pp = *(g+0x248) ; one source of truth (game_mem)
    if (!pp) return;

    PlayerInfo me; if (!read_player(me)) return;          // not in game yet -> keep cache/packets
    on_character_changed(me.id);                          // THE single reset point for a re-login as another character
    if (!cacheLoaded_ && me.id) load();                   // roster cache : needs the character, so it runs HERE, not at plugin init
    if (!pw_.valid) pw_.jobLevel = me.mlvl;               // PointWatch : seed the stage from memory so the box shows at once
    { unsigned cp = 0, jp = 0; if (read_capacity_points((unsigned)me.mjob, cp, jp)) { pw_.cpCur = cp; pw_.cpJp = (int)jp; pw_.cpMem = true; } }   // CP/JP from memory
    { PwMem pm; if (read_pointwatch(pm)) {                // XP / Exemplar / Limit Points / merits / Master Level from client memory -> all rows fill on load
        if (pm.mlOk)  pw_.masterLevel = pm.masterLevel;
        if (pm.xpOk)  { pw_.xpCur = pm.xpCur; pw_.xpTnl = pm.xpTnl; pw_.xpMem = true; }
        if (pm.epOk)  { pw_.epCur = pm.epCur; pw_.epTnml = pm.epTnml; pw_.epMem = true; }
        if (pm.merOk) { pw_.lpCur = pm.lpCur; pw_.merits = pm.merits; pw_.maxMerits = pm.maxMerits; pw_.merMem = true; }
    } }
    u32 base = self_party_base(me.id);                    // id-validated self anchor (one source of truth : game_mem)
    if (!base) return;                                     // unrecognised -> don't trust it

    // Entity position-object array (g+0x24) + the player's own horizontal position (member+0x20 =
    // entity index -> ent[idx] -> X @+0x04, Z @+0x0C). Used to fill each member's distance (yalms).
    u32 ent = entity_array();                             // *(g+0x24) ; one source of truth (game_mem)
    float px = 0.0f, pz = 0.0f;
    { u32 pidx = 0; safe_read(base + 0x20, &pidx); pidx &= 0xFFFF; entity_xz(ent, pidx, px, pz); }
    selfX_ = px; selfZ_ = pz;                            // expose the player's own position (target distance uses it)

    // ACTIVE member count = allianceinfo+0x13 (party-1 count). The member-array slots are NOT
    // cleared when a trust is dismissed (their id/HP linger), so the id!=0 scan over-reports
    // ghosts. Only the first `wantN` slots are live (verified in-game: 6 with trusts, 1 after
    // /refa all). This is the authoritative count XivParty's get_party() uses.
    int wantN = 6; u32 ai = 0;
    if (safe_read(pp, &ai) && valid_ptr(ai)) { u32 c = 0; if (safe_read(ai + 0x13, &c)) { wantN = (int)(c & 0xFF); if (wantN < 1) wantN = 1; if (wantN > 6) wantN = 6; } }

    int n = 0;
    for (int i = 0; i < wantN; ++i)                        // only the first `wantN` ACTIVE slots
        if (read_member(base + i * 0x7C, m[n], ent, px, pz)) ++n;
    count = n;                                             // n reflects the live roster (trust in/out)

    // OUT OF ZONE = the member is in a DIFFERENT zone, decided by the zone id, NOT by maxHp==0 (which a DEAD member
    // in our zone also hits -- MEASURED 2026-07-21: Kaories dead read zone=262=ours while Gab out-of-zone read 249).
    { const unsigned oz = zone_id();
      for (int i = 0; i < n; ++i) m[i].offzone = (m[i].zone != 0 && (unsigned)m[i].zone != oz); }

    // //aio bcaptlog : per-member field dump, throttled to 1/s. Two bugs to settle from real data (2026-07-21):
    //   (1) distance is sometimes garbage -> print the raw entity idx + its (x,z) vs ours ;
    //   (2) a DEAD member is shown as out-of-zone -> print hp/hpp/maxHp/zone so we see which field cleanly
    //       separates "dead in our zone" from "in another zone" (maxHp==0 conflates them).
    if (bcapt_armed()) {
        static unsigned s_ptLogMs = 0; const unsigned nowMs = GetTickCount();
        if ((int)(nowMs - s_ptLogMs) >= 1000) { s_ptLogMs = nowMs;
            // debug::log has NO %f (MEASURED : it printed "dist=f") -> everything as scaled INTEGERS. dist*10, pos*10.
            windower::debug::log("PARTY zone=%u self=(%d,%d) n=%d", zone_id(), (int)(px * 10), (int)(pz * 10), n);
            for (int i = 0; i < n; ++i) {
                u32 mb = base + i * 0x7C; u32 idx = 0; safe_read(mb + 0x20, &idx); idx &= 0xFFFF;
                u32 pent = 0; safe_read(ent + idx * 4, &pent); u32 sp = 0; if (valid_ptr(pent)) safe_read(pent + 0x1D0, &sp);   // raw SpawnType byte -> why is_trust may fail for ANOTHER player's trust
                windower::debug::log(
                    "  [%d] \"%s\" id=%08X isTrust=%d mjob=%d spawn=%02X hpp=%d zone=%d off=%d idx=%u",
                    i, m[i].name, m[i].id, m[i].isTrust, m[i].mjob, (unsigned)(sp & 0xFF), m[i].hpp, m[i].zone, m[i].offzone ? 1 : 0, idx);
            }
        }
    }

    if (me.id) {   // derived-state cache : restore once per CHARACTER, then persist throttled (~4s). Survives //unload+reload.
        // Keyed on cacheChar_, not a bare one-shot : after a re-login as someone else the old code kept
        // cacheLoaded_ = true, never read the new character's file, and 4 s later wrote the PREVIOUS character's
        // roll pips / song tags / buff casters into it -- destroying the real cache silently.
        if (!cacheLoaded_ || cacheChar_ != me.id) {
            // Latch cacheLoaded_ ONLY when load_cache RESOLVED (opened, or the file is genuinely absent). A transient
            // fopen failure -- the cache file momentarily LOCKED at the //unload+//load moment (AV, or the other
            // client finishing its own write to the shared path) -- used to latch anyway, losing roll pips / song
            // tags / buff casters for the whole session. Retry a bounded number of frames, then give up. (rule 10)
            static unsigned char s_cacheTries = 0;
            if (cacheChar_ != me.id) s_cacheTries = 0;   // new character -> fresh retry budget
            if (load_cache(me.id) || ++s_cacheTries >= 120) { cacheLoaded_ = true; cacheChar_ = me.id; lastCacheSaveMs_ = GetTickCount(); s_cacheTries = 0; }
        }
        const unsigned nowc = GetTickCount();
        if ((unsigned)(nowc - lastCacheSaveMs_) > 4000u) { save_cache(me.id); lastCacheSaveMs_ = nowc; }
    }

    // --- Alliance parties 2 & 3 : member-array slots 6..11 and 12..17. Live counts at
    //     allianceinfo +0x14 / +0x15 (mirror of +0x13 for our own party), gated by the party
    //     leader id (+0x08 / +0x0C). Jobs read straight from the member block (+0x71 / +0x73).
    for (int ap = 0; ap < 2; ++ap) {
        // SAFETY gate (VERIFIED offset): alliance party 2 / 3 exists only if its leader id is set
        // (allianceinfo +0x08 / +0x0C, same struct as read_party_leaders). Without this, a wrong
        // count byte could spawn phantom alliance boxes during normal solo/party play. No leader -> no box.
        u32 pleader = 0;
        if (valid_ptr(ai)) safe_read(ai + 0x08 + ap * 4, &pleader);
        if (!pleader) { alliN_[ap] = 0; continue; }
        int cnt = 0;                                          // member count : allianceinfo +0x14 / +0x15 (to verify in an alliance)
        if (valid_ptr(ai)) { u32 c = 0; if (safe_read(ai + 0x14 + ap, &c)) cnt = (int)(c & 0xFF); }
        if (cnt < 1) cnt = 6; if (cnt > 6) cnt = 6;           // gate already proved the party exists -> default to a full scan
        int an = 0;
        for (int i = 0; i < cnt; ++i)
            if (read_member(base + (6 + ap * 6 + i) * 0x7C, alli_[ap * 6 + an], ent, px, pz)) ++an;
        alliN_[ap] = an;
    }

    // JOB-CHANGE detection (Timers) : shadow every member's main/sub ; a change lists the id + clears their tracked
    // ally buffs. jobChanged_ is rebuilt each load (momentary : only the frame the job actually differs).
    jobChangedN_ = 0;
    for (int i = 0; i < count; ++i) note_member_job(m[i]);
    for (int ap = 0; ap < 2; ++ap) for (int i = 0; i < alliN_[ap]; ++i) note_member_job(alli_[ap * 6 + i]);
}

int PartyState::alliance_count(int tier) const {
    int idx = tier - 1;
    return (idx >= 0 && idx < 2) ? alliN_[idx] : 0;
}
const PMember& PartyState::alliance_member(int tier, int i) const {
    int idx = tier - 1; if (idx < 0) idx = 0; if (idx > 1) idx = 1;
    if (i < 0) i = 0; if (i > 5) i = 5;
    return alli_[idx * 6 + i];
}

} // namespace aio
