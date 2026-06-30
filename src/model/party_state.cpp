// party_state.cpp -- see party_state.h.
#include "model/party_state.h"
#include "model/game_mem.h"
#include "model/spells_gen.h"
#include "windower_debug.h"
#include <windows.h>
#include <cstring>
#include <math.h>

namespace aio {

using windower::safe_read;
using windower::valid_ptr;

static PartyState g_party;
PartyState& party() { return g_party; }

static int g_demoLevel = 0;
int  party_demo_level() { return g_demoLevel; }
void set_party_demo_level(int level) { g_demoLevel = level < 0 ? 0 : (level > 3 ? 3 : level); }

static int g_demoCount = 6;                                                  // party demo member count (1..6)
int  party_demo_count() { return g_demoCount; }
void set_party_demo_count(int c) { g_demoCount = c < 1 ? 1 : (c > 6 ? 6 : c); }

static bool g_demoSelect = false;
bool demo_select()            { return g_demoSelect; }
void set_demo_select(bool on) { g_demoSelect = on; }

static const char* CACHE = "D:\\Windower Tetsouo\\plugins\\aiohud_party.bin";

static const char* JOBS[] = {
    "", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD",
    "RNG", "SAM", "NIN", "DRG", "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN",
    "SPC"   // 23 : internal sentinel for special trusts (chemist/beast/etc.) -> XivParty's spc.png
};
const char* job_abbr(int id) { return (id >= 0 && id <= 23) ? JOBS[id] : ""; }

static bool streq(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int  job_id(const char* a) { if (!a || !a[0]) return 0; for (int i = 1; i <= 23; ++i) if (streq(JOBS[i], a)) return i; return 0; }

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

unsigned job_role_color(int id) {
    switch (id) {
        case 7: case 13: case 22:                      return 0xFF7D9BF0;  // PLD/NIN/RUN -> tank
        case 3: case 20:                               return 0xFF86D36F;  // WHM/SCH     -> healer
        case 5: case 10: case 15: case 17: case 21:    return 0xFFECC94A;  // RDM/BRD/SMN/COR/GEO -> support
        case 23:                                       return 0xFFB58BF0;  // SPC (special trusts) -> distinct purple
        default:                                       return 0xFFE08585;  // else -> dd
    }
}

static unsigned rd32(const unsigned char* p, int o) {
    return (unsigned)p[o] | ((unsigned)p[o+1] << 8) | ((unsigned)p[o+2] << 16) | ((unsigned)p[o+3] << 24);
}

int PartyState::find(unsigned id) const {
    for (int i = 0; i < count; ++i) if (m[i].id == id) return i;
    return -1;
}

void PartyState::on_dd(const unsigned char* p) {
    unsigned id = rd32(p, 0x04);
    if (!id) return;
    int i = find(id);
    if (i < 0) { if (count >= 6) return; i = count++; m[i] = PMember(); m[i].id = id; }
    m[i].hp    = (int)rd32(p, 0x08);
    m[i].mp    = (int)rd32(p, 0x0C);
    m[i].tp    = (int)rd32(p, 0x10);
    m[i].flags = rd32(p, 0x14);
    m[i].mpp = p[0x1D];
    m[i].hpp = p[0x1E];
    m[i].maxHp = m[i].hpp > 0 ? m[i].hp * 100 / m[i].hpp : m[i].hp;   // derive max -> 0x0DF can refresh %
    m[i].maxMp = m[i].mpp > 0 ? m[i].mp * 100 / m[i].mpp : m[i].mp;
    m[i].mjob = p[0x22];
    m[i].sjob = p[0x24];
    m[i].zone = (int)p[0x20] | ((int)p[0x21] << 8);   // member zone id (set when out of our zone)
    int j = 0; for (; j < 19 && p[0x28 + j]; ++j) m[i].name[j] = (char)p[0x28 + j];
    m[i].name[j] = 0;
    save();                                                          // roster changed -> cache it (rare)
}

void PartyState::on_df(const unsigned char* p) {
    int i = find(rd32(p, 0x04));
    if (i < 0) return;                        // only vitals for a member we already know
    m[i].hp = (int)rd32(p, 0x08);
    m[i].mp = (int)rd32(p, 0x0C);
    m[i].tp = (int)rd32(p, 0x10);
    if (m[i].hp > m[i].maxHp) m[i].maxHp = m[i].hp;   // track observed max (gear/buffs raise it)
    if (m[i].mp > m[i].maxMp) m[i].maxMp = m[i].mp;
    if (m[i].maxHp > 0) m[i].hpp = m[i].hp * 100 / m[i].maxHp;   // bar fill follows the value
    if (m[i].maxMp > 0) m[i].mpp = m[i].mp * 100 / m[i].maxMp;
}

// version tag tied to the struct size -> any PMember layout change auto-invalidates old caches.
static const int CACHE_VER = (int)(0xA10D0000u | (sizeof(PMember) & 0xFFFF));

void PartyState::save() const {
    HANDLE h = CreateFileA(CACHE, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; int ver = CACHE_VER;
    WriteFile(h, &ver, sizeof(ver), &w, 0);
    WriteFile(h, &count, sizeof(count), &w, 0);
    WriteFile(h, m, sizeof(PMember) * count, &w, 0);
    CloseHandle(h);
}

void PartyState::load() {
    HANDLE h = CreateFileA(CACHE, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
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
static bool read_member(u32 mb, PMember& pm, u32 ent, float px, float pz) {
    unsigned char b[0x7C];
    __try { memcpy(b, (const void*)mb, sizeof(b)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    const u32 id = *(const u32*)(b + 0x1C);
    if (!id) return false;                                  // empty slot
    pm = PMember();
    u32 idx = *(const unsigned short*)(b + 0x20);           // entity index
    if (ent && idx && idx < 0x900) {
        u32 p = 0;
        if (safe_read(ent + idx * 4, &p) && valid_ptr(p)) {
            u32 a = 0, c = 0; safe_read(p + 0x04, &a); safe_read(p + 0x0C, &c);
            float mx = *(float*)&a, mz = *(float*)&c;
            if (mx != 0.0f || mz != 0.0f) { float dx = mx - px, dz = mz - pz; pm.dist = sqrtf(dx * dx + dz * dz); }
        }
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
    const u32 jw = *(const u32*)(b + 0x70);                 // jobs: main job @+0x71, sub job @+0x73
    pm.mjob = (jw >> 8) & 0xFF; pm.sjob = (jw >> 24) & 0xFF;
    if (pm.mjob == 0) { int mj = 0, sj = 0; if (trust_job(pm.name, mj, sj)) { pm.mjob = mj; pm.sjob = sj; } }   // trusts: name DB fallback
    return true;
}

void PartyState::load_from_memory() {
    u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
    if (!lc) return;
    u32 g = 0, pp = 0;
    if (!safe_read(lc + 0x1C8400, &g) || !valid_ptr(g)) return;
    if (!safe_read(g + 0x248, &pp)   || !valid_ptr(pp)) return;

    PlayerInfo me; if (!read_player(me)) return;          // not in game yet -> keep cache/packets
    u32 base = pp - 4, id0 = 0;                            // self-validate the anchor against the player id
    if (!(safe_read(base + 0x1C, &id0) && id0 == me.id)) {
        if (safe_read(pp + 0x1C, &id0) && id0 == me.id) base = pp;   // tolerate a 0/-4 framing shift
        else return;                                       // unrecognised -> don't trust it
    }

    // Entity position-object array (g+0x24) + the player's own horizontal position (member+0x20 =
    // entity index -> ent[idx] -> X @+0x04, Z @+0x0C). Used to fill each member's distance (yalms).
    u32 ent = 0; safe_read(g + 0x24, &ent);
    float px = 0.0f, pz = 0.0f;
    { u32 pidx = 0; safe_read(base + 0x20, &pidx); pidx &= 0xFFFF;
      if (valid_ptr(ent) && pidx && pidx < 0x900) { u32 p = 0;
        if (safe_read(ent + pidx * 4, &p) && valid_ptr(p)) { u32 a = 0, c = 0; safe_read(p + 0x04, &a); safe_read(p + 0x0C, &c); px = *(float*)&a; pz = *(float*)&c; } } }

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

// ---- 0x028 action packet : drive the cast bar ------------------------------------------------
// Bit layout (verified vs Ivaar's actions.lua ; offsets in BITS from byte 0 of the packet):
//   actor id  @ bit 40 (byte 5), 32 bits      | category @ bit 82, 4 bits
//   target[0].action[0].param @ bit 213, 17 bits  -> spell id on a "begin casting" (category 8)
// category 8 = begin casting (magic) ; 4 = casting finished. Little-endian bit packing.
static unsigned getbits(const unsigned char* p, int bitoff, int width) {
    unsigned long long v = 0;
    int bo = bitoff >> 3;
    for (int i = 0; i < 8; ++i) v |= (unsigned long long)p[bo + i] << (8 * i);
    v >>= (bitoff & 7);
    return (unsigned)(v & ((1ull << width) - 1));
}

void PartyState::on_action(const unsigned char* p) {
    u32 hdr = (u32)p[0] | ((u32)p[1] << 8);
    int size = (int)((hdr >> 9) & 0x7F) * 4;               // packet size in bytes
    if (size < 30) return;                                 // begin-cast needs the action block (bit 213 -> byte 26..)
    u32 cat = getbits(p, 82, 4);
    if (cat != 8) return;                                  // only "begin casting" (cat 4 = finish arrives instantly here ;
                                                           // we DON'T clear on it -> the bar lives on its own cast time)
    u32 actor = getbits(p, 40, 32);
    u32 sid = getbits(p, 213, 17);                         // spell id
    const SpellRow* sp = spell_info(sid);
    // claim a cast slot for this actor (reuse same-id / free / oldest)
    int slot = -1, oldest = 0;
    for (int k = 0; k < 18; ++k) {
        if (casts_[k].id == actor) { slot = k; break; }
        if (!casts_[k].id) { if (slot < 0) slot = k; }
        if (casts_[k].startMs < casts_[oldest].startMs) oldest = k;
    }
    if (slot < 0) slot = oldest;
    casts_[slot].id = actor; casts_[slot].spell = sid;
    casts_[slot].startMs = GetTickCount(); casts_[slot].durMs = sp ? sp->cast_ms : 0;
}

const char* PartyState::cast_label(unsigned id, float& pctOut, float& alphaOut) const {
    pctOut = 0.0f; alphaOut = 0.0f;
    if (!id) return 0;
    const CastSlot* c = 0;
    for (int k = 0; k < 18; ++k) if (casts_[k].id == id && casts_[k].spell) { c = &casts_[k]; break; }
    if (!c) return 0;
    unsigned dur = c->durMs ? c->durMs : 1;
    unsigned el  = GetTickCount() - c->startMs;
    const unsigned FADE = 350;                             // pop-in / depop window (ms)
    if (el > dur + FADE) return 0;                         // fully gone
    pctOut = el >= dur ? 1.0f : (float)el / (float)dur;
    float a = 1.0f;
    if (el < 150)        a = (float)el / 150.0f;           // smooth POP in
    else if (el > dur)   a = 1.0f - (float)(el - dur) / (float)FADE;   // smooth DEPOP after the cast finishes
    alphaOut = a < 0.0f ? 0.0f : a;
    const SpellRow* sp = spell_info(c->spell);
    return sp ? sp->en : "Casting";
}

// 0x076 "party buffs" : 5 member slots of 48 bytes, payload at +4 (same base as 0x0DD). Per slot k:
//   id   = u32 @ k*48 + 4   (0 = empty slot ; the LOCAL player is never present here)
//   buff i (0..31) : low = p[k*48+20+i] ; hi2 = (p[k*48+12 + i/4] >> 2*(i&3)) & 3 ; buff = low + 256*hi2
//   255 = empty buff. (Credit: Kenshi/PartyBuffs + Byrth/GearSwap, via XivParty.)
void PartyState::on_076(const unsigned char* p) {
    for (int k = 0; k < 5; ++k) {
        const int base = k * 48;
        unsigned mid = rd32(p, base + 4);
        if (!mid) continue;
        int slot = -1, free = -1;                              // reuse same-id / first free / overwrite slot 0
        for (int s = 0; s < 18; ++s) {
            if (buffs_[s].id == mid) { slot = s; break; }
            if (!buffs_[s].id && free < 0) free = s;
        }
        if (slot < 0) slot = (free >= 0) ? free : 0;
        BuffSet& bs = buffs_[slot];
        bs.id = mid; bs.n = 0;
        for (int i = 0; i < 32; ++i) {
            unsigned low = p[base + 20 + i];
            unsigned hi2 = (p[base + 12 + (i >> 2)] >> (2 * (i & 3))) & 3;
            unsigned buff = low + 256u * hi2;
            if (buff == 255) continue;                         // empty buff
            bs.ids[bs.n++] = (unsigned short)buff;
        }
    }
}

const BuffSet* PartyState::buffs_for(unsigned id) const {
    if (!id) return 0;
    for (int s = 0; s < 18; ++s) if (buffs_[s].id == id) return &buffs_[s];
    return 0;
}

} // namespace aio
