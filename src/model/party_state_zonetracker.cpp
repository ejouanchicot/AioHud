// party_state_zonetracker.cpp -- Zone Tracker module (Dynamis / Abyssea / Omen / Nyzul / Sheol),
// split out of party_state.cpp. PURE MOVE : the PartyState::zt_*/on_omen_text/on_nyzul_text/
// nyzul_remaining/omen_short methods + their packet handlers (on_2a/on_55/on_118/on_034/on_00e)
// and the file-static helpers used only by them. See party_state.h for the ZoneTracker struct.
#include "model/party_state.h"
#include "model/party_state_internal.h"   // pkt_u16 / pkt_u32 (shared packet readers)
#include "model/paths.h"                  // plugin_path (the zone-cache path)
#include "model/game_mem.h"               // key_items_base (Limbus run baseline : the run's KIs) + entity_name_by_index
#include "windower_debug.h"               // warn when a >=1k award's source entity is not recognised as a coffer
#include <stdio.h>                        // snprintf (floor tags)
#include <windows.h>                       // GetTickCount / CreateFileA / ReadFile / WriteFile
#include <string.h>                        // strstr
#include <math.h>                          // floor()

namespace aio {

// LIMBUS : a per-kill payout runs ~40-110 units ; the coffer paid 3000 in the reference capture and 5000 is the
// payout worth calling out. So "in the thousands" separates a coffer from a kill -- a heuristic, but a wide one.
static const int LIMBUS_COFFER_MIN = 1000;
static const int LIMBUS_BIG_MIN    = 5000;
// Limbus run key items : 9956..9980 Temenos (Tem. N-F1 .. C-F4), 9981..9998 Apollyon (NW #1 .. SE #4). Owning NONE
// of them means no run is in progress -> the next entry starts a fresh count. Owning some means the previous run
// is still going, so the running totals must be KEPT (the user's rule).
static const int LIMBUS_KI_FIRST = 9956, LIMBUS_KI_LAST = 9998;

// "SW #4" -- the floor an event happened on, from bar1 of the 0x075 block.
static void limbus_floor_tag(const ZoneTracker& zt, char* out, int cap) {
    out[0] = 0;
    if (zt.limbusFloor < 0) return;
    // %.7s, not %.3s : Apollyon's quads are 2 letters ("SW") but Temenos spells its sides out ("North", "West"),
    // which used to render as "Nor #1".
    if (zt.limbusQuad[0]) snprintf(out, cap, "%.7s #%d", zt.limbusQuad, zt.limbusFloor);
    else                  snprintf(out, cap, "#%d", zt.limbusFloor);
}

// How many Limbus run key items are owned right now (-1 = the KI store is not reachable yet).
static int limbus_ki_owned() {
    if (!key_items_base()) return -1;                      // store not reachable yet -> "unknown", never "zero"
    int n = 0;
    for (int id = LIMBUS_KI_FIRST; id <= LIMBUS_KI_LAST; ++id) if (owns_key_item((unsigned)id)) ++n;
    return n;
}

// ---- Limbus coffer history : its OWN file. The zone cache cannot hold this (single shared file, restored only
// when curZone matches -> a Dynamis run in between would wipe it). Version tag embeds the struct size so a layout
// change auto-invalidates, same trick as ZT_CACHE_VER.
static const int LC_VER = (int)(0x4C430000u | ((sizeof(LimbusCoffers) * 2) & 0xFFFF));
// PER-CHARACTER cache paths. These files used to be ONE PER INSTALL ("zone.bin" / "limbus.bin"), so every
// character on the same Windower shared them : the last one to save won, and the next character loaded its
// values -- Kaories' Temenos units showing up on Tetsouo. Key them on the character's server id.
//
// Deliberately NOT cached in a static : one client can log out and back in as another character, and a
// remembered path would keep writing the previous character's file. Returns false when no character is
// resolved yet (not logged in / zoning) -> callers skip the save or load entirely rather than touch a shared
// file. read_player is SEH-guarded and these run on events, not per frame, so the cost is irrelevant.
static bool char_cache_path(const char* leaf, char* out, int cap) {
    PlayerInfo me;
    if (!read_player(me) || !me.id) return false;
    char rel[80]; _snprintf(rel, sizeof(rel), "data\\cache\\%s_%u.bin", leaf, me.id); rel[sizeof(rel) - 1] = 0;
    plugin_path(out, cap, rel);
    return out[0] != 0;
}

bool PartyState::limbus_add_chip(int area, const char* quad, int amtK) {
    const int a = (area == 1) ? 1 : 0, slot = limbus_slot_of(a, quad);
    if (slot < 0 || amtK <= 0) return false;
    LimbusCoffers& lc = lc_[a];
    if (amtK >= (LIMBUS_BIG_MIN / 1000)) {                 // a 5k ends the cycle, hand-seeded or not
        for (int i = 0; i < 4; ++i) lc.prevK[i] = lc.slotK[i];   // archive first (see prevK in party_state.h)
        for (int i = 0; i < 4; ++i) lc.slotK[i] = 0;
        lc.bigAmt = amtK * 1000;
        int b = 0; for (; quad[b] && b < 7; ++b) lc.bigAt[b] = quad[b]; lc.bigAt[b] = 0;
    }
    lc.slotK[slot] = (unsigned char)amtK;
    lc_save();
    return true;
}
void PartyState::lc_save() const {
    char path[300]; if (!char_cache_path("limbus", path, sizeof(path))) return;   // no character -> never write a shared file
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; int ver = LC_VER;
    WriteFile(h, &ver, sizeof(ver), &w, 0);
    WriteFile(h, lc_, sizeof(lc_), &w, 0);
    CloseHandle(h);
}
void PartyState::lc_load() {
    char path[300]; if (!char_cache_path("limbus", path, sizeof(path))) return;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    int ver = 0; LimbusCoffers c[2]; DWORD got = 0;
    if (ReadFile(h, &ver, sizeof(ver), &got, 0) && got == sizeof(ver) && ver == LC_VER &&
        ReadFile(h, c, sizeof(c), &got, 0) && got == sizeof(c)) { lc_[0] = c[0]; lc_[1] = c[1]; }
    CloseHandle(h);
}

// OMEN : parse a mode-161 objective line (ported from the Omen addon's text patterns). The number wanted is the one
// preceding a keyword starting with the type's "check letter" (the addon's `check` pattern) -> skips the leading "N:".
static int omen_num_before(const char* s, char letter) {
    for (int i = 0; s[i]; ) {
        if (s[i] >= '0' && s[i] <= '9') {
            long v = 0; int j = i; while (s[j] >= '0' && s[j] <= '9') { v = v * 10 + (s[j] - '0'); ++j; }
            int k = j; while (s[k] == ' ') ++k;
            if (s[k] == letter) return (int)v;
            i = j;
        } else ++i;
    }
    return -1;
}
static int omen_first_num(const char* s) {
    for (int i = 0; s[i]; ++i) if (s[i] >= '0' && s[i] <= '9') { long v = 0; while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; } return (int)v; }
    return -1;
}
static const char* OMEN_SHORT[15] = { "", "WS Damage", "MB Damage", "Non-MB Nuke", "Melee Round", "Kills",
    "Critical Hits", "Abilities", "Spells", "Magic Bursts", "Skillchains", "All WS", "Physical WS", "Magic WS", "500 HP Cures" };
const char* PartyState::omen_short(int t) { return (t >= 1 && t <= 14) ? OMEN_SHORT[t] : ""; }
static void omen_reset_objs(ZoneTracker& zt) { for (int k = 0; k < 10; ++k) zt.omen[k] = ZoneTracker::OmenObj{}; }
static void omen_set_floor(ZoneTracker& zt, const char* s) { int w = 0; for (int i = 0; s[i] && w < 47; ++i) { char c = s[i]; if (c >= 0x20 && c < 0x7F) zt.floorObj[w++] = c; } zt.floorObj[w] = 0; }

void PartyState::on_omen_text(const char* s) {
    if (zt_.mode != 3 || !s) return;
    if (s[0] >= '1' && s[0] <= '9') {                              // "N: <objective>" line
        int slot = 0, i = 0; while (s[i] >= '0' && s[i] <= '9') { slot = slot * 10 + (s[i] - '0'); ++i; }
        if (s[i] != ':' || slot < 1 || slot > 10) return;
        if (strstr(s, "You have failed")) return;                  // fail line -> ignore
        const bool eval = strstr(s, "You have") != 0;
        struct T { const char* sub; int id; char c; };
        static const T TT[] = {                                    // distinctive tails, specific-first ; c = check letter
            { "without performing a magic burst", 3, 'u' }, { "single magic burst", 2, 'u' }, { "single weapon skill", 1, 'u' },
            { "single auto-attack", 4, 'i' }, { "killchain", 10, 's' }, { "elemental weapon", 13, 'e' }, { "physical weapon", 12, 'p' },
            { "weapon skill", 11, 'w' }, { "critical", 6, 'c' }, { "magic burst", 9, 'm' }, { "anquish", 5, 'f' },
            { "500 HP", 14, 't' }, { "pell", 8, 's' }, { "bilit", 7, 'a' } };
        int typeId = 0; char letter = 0;
        for (unsigned k = 0; k < sizeof(TT) / sizeof(TT[0]); ++k) if (strstr(s, TT[k].sub)) { typeId = TT[k].id; letter = TT[k].c; break; }
        if (!typeId) return;
        const int num = omen_num_before(s, letter);
        ZoneTracker::OmenObj& o = zt_.omen[slot - 1];
        if (eval) { if (num >= 0) o.cur = num; if (o.type == 0) { o.type = typeId; o.req = -1; } }
        else      { if (o.type != typeId) o.cur = 0; o.type = typeId; if (num >= 0) o.req = num; }
        zt_save();
        return;
    }
    if (strstr(s, "seconds remaining")) { const int n = omen_first_num(s); if (n >= 0) { zt_.omenBonusSec = n; zt_.omenBonusMs = GetTickCount(); zt_save(); } return; }
    if (strstr(s, " omen")) { const int n = omen_first_num(s); if (n >= 0) { zt_.omens = n; zt_save(); } return; }
    if (strstr(s, "spectral light flares up")) { zt_.omenCleared = 1; zt_save(); return; }
    if (strstr(s, "light shall come even if you fail")) { omen_set_floor(zt_, "Free Floor!"); if (zt_.omenCleared) { omen_reset_objs(zt_); zt_.omenCleared = 0; } zt_save(); return; }
    if (strstr(s, "Vanquish") || strstr(s, "treasure portent")) { omen_set_floor(zt_, s); if (zt_.omenCleared) { omen_reset_objs(zt_); zt_.omenCleared = 0; } zt_save(); return; }
}

// ---- NYZUL ISLE : token estimator + floor/timer/objective tracker, ported 1:1 from the NyzulHelper addon (Glarin
// of Asura). All values are derived from the run's incoming-text lines ; token math mirrors the addon exactly. ----
static int  ny_round(double x) { return (int)floor(x + 0.5); }
static void ny_set_timer(ZoneTracker& z, int remaining) { z.nyTimerSec = remaining; z.nyTimerMs = GetTickCount(); }
static int  ny_relative_floor(const ZoneTracker& z) { return (z.nyFloor < z.nyStartFloor) ? z.nyFloor + 100 : z.nyFloor; }
static double ny_token_rate(const ZoneTracker& z) {                    // +10% armband ; -10% per party member over 3
    double r = 1.0;
    if (z.nyArmband) r += 0.1;
    if (z.nyPartySize > 3) r -= (z.nyPartySize - 3) * 0.1;
    return r;
}
static void ny_calc_tokens(ZoneTracker& z) {                           // accrue this floor's reward at clear time
    const int rel = ny_relative_floor(z);
    const double rate = ny_token_rate(z);
    const double floorBonus = (rel > 1) ? 10.0 * (double)((rel - 1) / 5) : 0.0;
    const double penalty = (double)(ny_round(117.0 * rate) * z.nyPenalties);
    z.nyTokens += (200.0 + floorBonus) * rate - penalty;
}
static void ny_resync(ZoneTracker& z) {                               // on floor arrival : seed start floor + reconcile
    if (z.nyStartFloor == 0) { z.nyStartFloor = z.nyFloor; if (z.nyTimerMs == 0) ny_set_timer(z, 1800); }
    const int rel = ny_relative_floor(z);
    if (rel - z.nyStartFloor > z.nyCompleted) z.nyCompleted = rel - z.nyStartFloor;
    z.nyPenalties = 0;
}
static void ny_reset_run(ZoneTracker& z) {                           // addon reset() : clears the run, NOT armband/party
    z.nyTimerSec = 0; z.nyTimerMs = 0;
    z.nyObjective[0] = 0; z.nyRestriction[0] = 0; z.nyObjPending = 1; z.nyRestrFail = 0;
    z.nyStartFloor = 0; z.nyFloor = 0; z.nyCompleted = 0; z.nyPenalties = 0; z.nyTokens = 0.0;
}
static bool ny_has_ki(const unsigned char* p, unsigned kiId) {       // 0x055 : bitfield @0x04, table @0x84 (512 KIs/table)
    const unsigned table = kiId / 512, bit = kiId % 512;
    const unsigned ty = (unsigned)p[0x84] | ((unsigned)p[0x85] << 8) | ((unsigned)p[0x86] << 16) | ((unsigned)p[0x87] << 24);
    if (ty != table) return false;
    return (p[0x04 + bit / 8] >> (bit % 8)) & 1;
}
// strip FFXI format/control bytes -> printable ASCII, skipping `skip` leading source chars + trimming spaces. The
// format codes 0x1E/0x1F/0x7F each carry a 1-byte argument that must ALSO be dropped (else e.g. the "\x7F\x31" line
// terminator leaks a stray '1' at the tail) -- mirrors the addon's strip_format (0x1E./0x1F./0x7F. gsubs).
static void ny_copy_text(char* dst, int cap, const char* s, int skip) {
    int w = 0;
    for (int i = 0; s[i] && w < cap - 1; ++i) {
        if (i < skip) continue;
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1E || c == 0x1F || c == 0x7F) { if (s[i + 1]) ++i; continue; }   // format code + its 1-byte arg
        if (c >= 0x20 && c < 0x7F) dst[w++] = (char)c;
    }
    while (w > 0 && dst[w - 1] == ' ') --w;                                          // trim trailing spaces
    dst[w] = 0;
    int st = 0; while (dst[st] == ' ') ++st;                                         // trim leading spaces (from `skip`)
    if (st) { int j = 0; for (;;) { dst[j] = dst[st + j]; if (!dst[st + j]) break; ++j; } }
}

int PartyState::nyzul_remaining() const {
    if (zt_.nyTimerMs == 0) return 0;
    return zt_.nyTimerSec - (int)((GetTickCount() - zt_.nyTimerMs) / 1000u);
}

// ---- ZONE-TRACKER cache : the reference addons lose everything on reload ; we don't. Every tracked mode's timer is a
// GetTickCount stamp (dynEntryMs / visitantMs / omenBonusMs / nyTimerMs), and GetTickCount is SYSTEM uptime -> it keeps
// counting across a DLL unload/reload (same game process), so the whole run stays live. We persist the ENTIRE
// ZoneTracker (all four modes) and restore it ONLY on a fresh plugin load while already standing in the SAME zone (see
// zt_set_zone) ; a new run always re-enters via a zone change, so it never picks up a stale cache. ----
// (the zone cache is PER CHARACTER via char_cache_path -- see the note there ; it used to be one shared file)
static const int ZT_CACHE_VER = (int)(0x5A540000u | (sizeof(ZoneTracker) & 0xFFFF));   // 'ZT' | struct size -> auto-invalidate
void PartyState::zt_save() const {
    if (zt_.mode == 0) return;                              // nothing to persist outside a tracked zone
    char path[300]; if (!char_cache_path("zone", path, sizeof(path))) return;   // no character -> never write a shared file
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; int ver = ZT_CACHE_VER;
    WriteFile(h, &ver, sizeof(ver), &w, 0);
    WriteFile(h, &zt_, sizeof(zt_), &w, 0);                 // ZoneTracker is trivially copyable (POD) -> raw blob
    CloseHandle(h);
}
bool PartyState::zt_load(int zone) {
    char path[300]; if (!char_cache_path("zone", path, sizeof(path))) return false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    int ver = 0; ZoneTracker c; DWORD got = 0; bool ok = false;
    if (ReadFile(h, &ver, sizeof(ver), &got, 0) && got == sizeof(ver) && ver == ZT_CACHE_VER &&
        ReadFile(h, &c, sizeof(c), &got, 0) && got == sizeof(c)) {
        // must be the SAME zone we're re-entering + a real tracked mode ; a reboot resets GetTickCount, leaving the
        // stored stamps in the "future" (> now) -> reject as stale.
        const unsigned now = GetTickCount();
        const bool freshTimers = (c.dynEntryMs <= now && c.visitantMs <= now && c.omenBonusMs <= now && c.nyTimerMs <= now);
        if (c.curZone == zone && c.mode != 0 && freshTimers) { zt_ = c; ok = true; }
    }
    CloseHandle(h);
    return ok;
}
void PartyState::on_nyzul_text(const char* s, int mode) {
    if (zt_.mode != 4 || !s || !s[0]) return;
    bool ch = false;                                          // only a real state change triggers a disk write (these modes spam)
    if (mode == 123) {
        if (strstr(s, "Security field malfunction")) { ny_copy_text(zt_.nyRestriction, 40, s, 0); zt_.nyRestrFail = 1; ch = true; }
        else if (strstr(s, "Time limit has been reduced")) { const int n = omen_first_num(s); if (n >= 0) { ny_set_timer(zt_, nyzul_remaining() - n * 60); ch = true; } }
        else if (strstr(s, "Potential token reward reduced")) { zt_.nyPenalties++; ch = true; }
    } else if ((mode == 146 || mode == 148) && strstr(s, "(Earth time)")) {   // "... N minute(s) (Earth time)" -> set timer
        const int mult = strstr(s, "minute") ? 60 : 1; const int n = omen_first_num(s); if (n >= 0) { ny_set_timer(zt_, n * mult); ch = true; }
    } else if (mode == 146) {
        if (strstr(s, "objective complete. Rune of Transfer activated")) {
            zt_.nyCompleted++; zt_.nyObjPending = 0; zt_.nyRestriction[0] = 0; zt_.nyRestrFail = 0; ny_calc_tokens(zt_); ch = true;
        }
    } else if (mode == 148) {
        if (strstr(s, "Objective:")) {
            if (strstr(s, "Commencing")) { const char* o = "Complete on-site objectives"; int k = 0; for (; o[k] && k < 47; ++k) zt_.nyObjective[k] = o[k]; zt_.nyObjective[k] = 0; }
            else ny_copy_text(zt_.nyObjective, 48, s, 10);   // skip the "Objective:" prefix
            zt_.nyObjPending = 1; ch = true;
        } else if (strstr(s, "archaic")) { ny_copy_text(zt_.nyRestriction, 40, s, 0); ch = true; }   // a floor restriction (keeps warning colour)
        else if (strstr(s, "Welcome to Floor")) { const int n = omen_first_num(s); if (n >= 0) { zt_.nyFloor = n; ny_resync(zt_); ch = true; } }
    }
    if (ch) zt_save();
}

static void omen_reset_objs(ZoneTracker& zt);            // fwd : zt_set_zone resets the Omen objectives on entry
static void omen_set_floor(ZoneTracker& zt, const char* s);

// ZONE TRACKER : Dynamis time-extension tables (minutes per owned granule KI ; city zones vs the rest).
static const int ZT_CITY_TE[5]  = { 10, 10, 10, 15, 15 };   // Crimson / Azure / Amber / Alabaster / Obsidian
static const int ZT_OTHER_TE[5] = { 10, 10, 10, 10, 20 };
void PartyState::zt_recompute_dyn_limit() {
    const int z = zt_.dynZone;
    const bool city  = (z == 185 || z == 186 || z == 187 || z == 188);
    const bool other = (z == 134 || z == 135 || z == 39 || z == 40 || z == 41 || z == 42);
    int lim = 3600;
    if (city || other) { const int* TE = city ? ZT_CITY_TE : ZT_OTHER_TE; for (int i = 0; i < 5; ++i) if (zt_.ki[i]) lim += TE[i] * 60; }
    zt_.dynLimitSec = lim;
}
void PartyState::zt_set_zone(int zone, const char* name) {
    if (zone == zt_.curZone) return;                        // no transition
    for (int i = 0; i < 10; ++i) treasure_[i] = TreasureItem{};   // Treasure Pool is ZONE-specific : empty it on ANY zone change (runs every frame here, module-independent). No packet clears the old zone's items, so without this they linger as a phantom pool until their ~5-min expiry.
    // CHARACTER SWITCH on the same client (log out -> log in as someone else). The per-character cache files are
    // only re-read when entering a tracked zone, so until then zt_/lc_ still hold the PREVIOUS character's run --
    // and any save in between would write their data into the new character's file, re-creating by another route
    // exactly the cross-contamination the per-character split fixes. Wipe both and force the restore path to run
    // again for the new character.
    {
        static unsigned s_lastChar = 0;
        PlayerInfo who;
        const unsigned cur = (read_player(who) && who.id) ? who.id : 0;
        if (cur && s_lastChar && cur != s_lastChar) {
            zt_ = ZoneTracker{}; lc_[0] = LimbusCoffers{}; lc_[1] = LimbusCoffers{};
            zt_.curZone = -1;                               // -1 -> the one-shot restore below re-runs for this character
        }
        if (cur) s_lastChar = cur;
    }
    const int oldZone = zt_.curZone;
    const int prevMode = zt_.mode;
    // Fresh plugin load (crash/reload) while ALREADY standing in a tracked zone -> restore the cached run instead of
    // resetting it. A NEW run always re-enters via a real zone change (oldZone >= 0), so it never restores a stale cache.
    // The restore is a ONE-SHOT (it only fires while curZone is still -1), and the cache is now per character, so
    // it needs the character resolved. Right after a plugin load the player struct can be a frame or two behind ;
    // consuming the one-shot then would silently lose a mid-run reload. Wait instead -- leave curZone at -1 and
    // retry on the next poll.
    if (oldZone < 0) {
        char probe[300];
        if (!char_cache_path("zone", probe, sizeof(probe))) return;   // character not ready yet -> retry next frame
        if (zt_load(zone)) { zt_.curZone = zone; return; }
    }
    zt_.curZone = zone;
    int mode = 0;
    if (name) { if (name[0] == 'D' && name[1] == 'y' && name[2] == 'n') mode = 1;         // "Dynamis..."
                else if (name[0] == 'A' && name[1] == 'b' && name[2] == 'y') mode = 2; }  // "Abyssea..."
    if (zone == 292) mode = 3;                                                            // Reisenjima Henge (Omen)
    if (zone == 77)  mode = 4;                                                            // Nyzul Isle (Uncharted Area)
    if ((zone == 298 || zone == 279) && oldZone == 247) mode = 5;                         // Sheol A/B/C -- ONLY from Rabao (298/279 are also Selbina HTMBs)
    if (zone == 38 || zone == 37) mode = 6;                                               // Limbus : Apollyon (38) / Temenos (37)
    if (mode == 3) {
        if (zt_.mode != 3) { omen_reset_objs(zt_); zt_.omens = 0; zt_.omenBonusSec = 0; zt_.omenCleared = 0; omen_set_floor(zt_, "Waiting for objectives..."); }
        zt_.mode = 3;
    } else if (mode == 1) {
        if (zt_.mode != 1) for (int i = 0; i < 5; ++i) zt_.ki[i] = 0;                     // fresh run
        zt_.mode = 1; zt_.dynZone = zone; zt_.dynEntryMs = GetTickCount();
        zt_recompute_dyn_limit();
    } else if (mode == 2) {
        // fresh entry : no visitant status yet -> the 5-minute EXPULSION grace timer (a visitant message overwrites it)
        if (zt_.mode != 2) { for (int i = 0; i < 7; ++i) zt_.lights[i] = 0; zt_.visitantMin = 5; zt_.visitantMs = GetTickCount(); }
        // client message base drifted +23 from the addon's old message_ids (7315 -> 7338), confirmed live via
        // //aio abylog : the two /heal bulk reports landed at rel 0/1 (mid 7338/7339) and visitant at rel 9/10 (7348).
        zt_.mode = 2; zt_.abyOffset = (zone == 215 || zone == 253) ? 7238 : 7338;
    } else if (mode == 4) {
        // A reload mid-run was already handled by zt_load() above ; reaching here means a genuine NEW entry -> clear it.
        if (prevMode != 4) { ny_reset_run(zt_); zt_.nyPartySize = (count > 0) ? count : 1; }
        zt_.mode = 4;
    } else if (mode == 5) {
        // Fresh Sheol entry from Rabao -> new run : the FIRST 0x02A msg-40016 self-baselines the counter (segBase =
        // its p2-p1 = the banked total before the first kill), so start unset. KEEP zt_.sheolzone : on_034 set it at
        // the Rabao conflux menu just before this zone change, and it must carry into the run (cleared on exit, below).
        zt_.segBase = -1;
        zt_.segments = 0; zt_.segLastRun = 0;
        zt_.mode = 5;
    } else if (mode == 6) {
        // Fresh Limbus entry -> clear the run : the name + level arrive on the first 0x075 menu packet (on_limbus_075) ;
        // progress/floor stay -1 (placeholders) until the memory phase fills them.
        if (prevMode != 6) {
            zt_.limbusArea[0] = 0; zt_.limbusLevel = 0; zt_.limbusProgress = -1; zt_.limbusQuad[0] = 0; zt_.limbusFloor = -1;
            // Run accounting is baselined on KEY-ITEM ownership, not on zoning : the run KIs (9956..9998) are granted
            // per cleared floor and wiped in bulk when the run ends. Owning none => this is a fresh run, so the
            // totals restart at zero ; owning some => the previous run is still live and its count must survive the
            // zone (the user's rule). A KI store that is not reachable yet (-1) is treated as "keep" -- never wipe
            // a real count on a failed read.
            lc_load();                                     // chips live in their own file -> restore on every entry
            if (limbus_ki_owned() == 0) {
                zt_.limbusRunUnits = 0; zt_.limbusUnitBase = -1;   // -1 -> re-baseline off the next payout's total
                zt_.limbusCofferAmt = 0; zt_.limbusCofferAt[0] = 0;
            }
        }
        zt_.mode = 6;
    } else {
        // Sheol -> Rabao (247) : FREEZE the run total as "N (last run)" (addon's conserve) + clear A/B/C for the next
        // run. Any other exit resets everything.
        if (prevMode == 5 && zone == 247) { zt_.mode = 5; zt_.segLastRun = 1; zt_.sheolzone = 0; }
        else {
            if (prevMode == 5) { zt_.segments = 0; zt_.segLastRun = 0; zt_.segBase = -1; zt_.sheolzone = 0; }
            // Nyzul -> staging (77 -> 72) : addon keeps the run but zeroes the timer + armband ; any other exit resets it.
            if (oldZone == 77 && zone == 72) { zt_.nyTimerSec = 0; zt_.nyTimerMs = 0; zt_.nyArmband = 0; }
            else if (prevMode == 4) ny_reset_run(zt_);
            zt_.mode = 0;
        }
    }
    if (zt_.mode != 0) zt_save();                            // snapshot on entry into any tracked zone (Dynamis/Abyssea/Omen/Nyzul)
}
void PartyState::on_034(const unsigned char* p) {           // 0x034 NPC interaction : the Rabao conflux entry menu -> which Sheol (A/B/C)
    if (zt_.curZone != 247) return;                        // only the Rabao (247) entry menu
    if (pkt_u16(p, 0x2C) != 173) return;                   // Menu ID 173 = the Odyssey conflux
    if (pkt_u32(p, 0x04) != selfId_) return;               // ...interacting with US
    const int i = (int)pkt_u32(p, 0x08);                   // Menu Parameters[0] = 1/2/3 = Sheol A/B/C
    if (i > 0 && i < 4) { zt_.sheolzone = i; zt_save(); }  // set now (in Rabao) ; kept through the zone-in
}
void PartyState::on_00e(const unsigned char* p) {          // 0x00E NPC update : fallback A/B/C from a mob's instance bits (menu missed)
    if (zt_.mode != 5 || zt_.sheolzone) return;            // only inside a Sheol run, and only while still unknown
    const unsigned id = pkt_u32(p, 0x04);                  // the entity's server id
    if (id < 0x01000000u) return;
    const unsigned instance = (id >> 12) & 0xFFFu;         // unique instance bits (addon : bit.band(bit.rshift(id,12),0xFFF))
    static const unsigned INST[3][2] = { {1019, 1020}, {1021, 1022}, {1023, 1024} };   // Sheol A / B / C
    for (int k = 0; k < 3; ++k)
        if (instance == INST[k][0] || instance == INST[k][1]) { zt_.sheolzone = k + 1; zt_save(); break; }
}
void PartyState::on_limbus_075(const unsigned char* p) {    // 0x075 : battlefield timer/BARS -> Limbus area/level + floor + gauge
    if (zt_.mode != 6) return;                             // only while standing in a Limbus zone (38 Apollyon / 37 Temenos)
    // bar[i] = { s32 progress ; char label[16] } at +0x28 + i*0x14, six of them (Windower's fields.lua documents
    // five). 0x075 is multiplexed -- other senders put position floats here -- so we self-filter on the LABEL of
    // each bar and only write a field whose label actually matched. A bar the server left empty reads label[0]==0
    // (the client's own renderer skips those) and its progress is a sentinel (-1 / 0x7FFFFFFF), never 0..100.
    bool hit = false;
    for (int i = 0; i < 6; ++i) {
        const int off = 0x28 + i * 0x14;
        const int prog = (int)pkt_u32(p, off);
        char lbl[17];                                      // label is a fixed 16-byte field : may be unterminated
        int n = 0;
        for (; n < 16; ++n) { char c = (char)p[off + 4 + n]; if (c < 0x20 || (unsigned char)c >= 0x7F) break; lbl[n] = c; }
        lbl[n] = 0;
        if (n < 3) continue;                               // empty / garbage -> the server did not fill this bar
        // "<Area>_Lv<NNN>" -> area + level (bar 0 in practice ; matched by content, not by index)
        int cut = -1;
        for (int k = 0; k + 2 < n; ++k) if (lbl[k] == '_' && lbl[k + 1] == 'L' && lbl[k + 2] == 'v') { cut = k; break; }
        if (cut > 0 && (lbl[0] == 'A' || lbl[0] == 'T')) {
            int w = 0; for (; w < cut && w < 15; ++w) zt_.limbusArea[w] = lbl[w]; zt_.limbusArea[w] = 0;
            int lvl = 0; for (const char* d = lbl + cut + 3; *d >= '0' && *d <= '9'; ++d) lvl = lvl * 10 + (*d - '0');
            zt_.limbusLevel = lvl; hit = true;
            continue;
        }
        // The floor bar -- THIS bar's progress is the gauge. TWO label formats, one per wing :
        //   Apollyon : "<Quad>_Floor_#<N>"   e.g. "SW_Floor_#3"
        //   Temenos  : "<Side>_Tower_F<N>"   e.g. "North_Tower_F1", "West_Tower_F3"   (captured 2026-07-19)
        // Only "Floor" was matched, so in Temenos NO bar ever matched and limbusProgress stayed -1 -> no gauge drawn,
        // while the "<Area>_Lv<NNN>" bar still matched and the wing name showed. That is exactly the reported
        // "Temenos shows the name but never a progress bar". Match either keyword.
        int fpos = -1;
        for (int k = 0; k + 5 <= n; ++k)
            if ((lbl[k]=='F' && lbl[k+1]=='l' && lbl[k+2]=='o' && lbl[k+3]=='o' && lbl[k+4]=='r') ||
                (lbl[k]=='T' && lbl[k+1]=='o' && lbl[k+2]=='w' && lbl[k+3]=='e' && lbl[k+4]=='r')) { fpos = k; break; }
        if (fpos < 0) continue;
        int q = 0;                                         // everything before "Floor", minus the separator
        for (; q < fpos && q < 7; ++q) { char c = lbl[q]; if (c == '_' || c == ' ') break; zt_.limbusQuad[q] = c; }
        zt_.limbusQuad[q] = 0;
        int fl = -1;
        for (int k = fpos; k < n; ++k) if (lbl[k] >= '0' && lbl[k] <= '9') { fl = 0; for (; k < n && lbl[k] >= '0' && lbl[k] <= '9'; ++k) fl = fl * 10 + (lbl[k] - '0'); break; }
        zt_.limbusFloor = fl;
        zt_.limbusProgress = (prog >= 0 && prog <= 100) ? prog : -1;   // sentinels (-1 / 0x7FFFFFFF) = gauge inactive
        hit = true;
    }
    if (hit) zt_save();
}
void PartyState::on_118(const unsigned char* p) {           // 0x118 currency2 : Mog Segments @byte 0x8C (reference total)
    // The banked total is NOT pushed live during a run (no 0x118 fires per kill) -> the run counter is driven by the
    // 0x02A msg-40016 per-kill message (on_2a). We only keep segBank here for reference (e.g. the Rabao "(last run)").
    zt_.segBank = (int)pkt_u32(p, 0x8C);
    zt_.limbusTemenos  = (int)pkt_u32(p, 0x98);      // Limbus currencies ride the same currency2 packet
    zt_.limbusApollyon = (int)pkt_u32(p, 0x9C);      // (fields.lua incoming[0x118] : 'Temenos Units' / 'Apollyon Units')
}
void PartyState::on_55(const unsigned char* p) {            // 0x055 : key items ; granules are Type 3, bits 9..13
    if (zt_.curZone == 72 && ny_has_ki(p, 797)) zt_.nyArmband = 1;   // Nyzul assault armband, captured in the staging point
    if (zt_.mode != 1) return;
    if (pkt_u32(p, 0x84) != 3) return;
    for (int i = 0; i < 5; ++i) { const int bit = 9 + i; zt_.ki[i] = (unsigned char)((p[0x04 + bit / 8] >> (bit % 8)) & 1); }
    zt_recompute_dyn_limit();
    zt_save();                                             // KIs / time-extensions changed -> persist
}
void PartyState::on_2a(const unsigned char* p) {            // 0x02A : Sheol segments (mode 5) + Abyssea zone messages (mode 2)
    // SHEOL / ODYSSEY segments : msg **40016** (masked 0x7FFF = 7248) carries p1 = segments THIS kill, p2 = the RUNNING
    // banked total. segments = p2 - baseline (baseline = p2-p1 at the first message) -> IDEMPOTENT (a duplicate chunk
    // shares p2 so it can't double-count) and packet-loss-proof (p2 is authoritative). The banked total does NOT push
    // a live 0x118, so this per-kill message is the only live source (confirmed via //aio sheollog 2026-07-10 : 40016
    // p2 = 947498/947511/947524, p1=13, baseline 947485). The msg id drifts every patch (was 40005/40015...) -> if the
    // counter stops after a client update, //aio sheollog + find the new id whose p2 tracks your banked total.
    if (zt_.mode == 5) {
        if (!zt_.segLastRun && (pkt_u16(p, 0x1A) & 0x7FFFu) == 7248) {
            const int gain = (int)pkt_u32(p, 0x08), total = (int)pkt_u32(p, 0x0C);
            if (zt_.segBase < 0) zt_.segBase = total - gain;   // baseline = banked total BEFORE this (first-seen) kill
            zt_.segments = (total > zt_.segBase) ? (total - zt_.segBase) : 0;
            zt_save();
        }
        return;
    }
    // LIMBUS (mode 6) : the run economy. See the field block in party_state.h for the message map. Everything is
    // tagged with the CURRENT floor (bar1 of the 0x075 block, already parsed into limbusQuad/limbusFloor) -- that
    // cross of the two channels is what answers "which floor did this drop on".
    if (zt_.mode == 6) {
        const unsigned m = pkt_u16(p, 0x1A) & 0x3FFFu;
        const int p1 = (int)pkt_u32(p, 0x08), p3 = (int)pkt_u32(p, 0x10), p4 = (int)pkt_u32(p, 0x14);
        char at[12]; limbus_floor_tag(zt_, at, sizeof(at));
        // Units acquired. NOTE (2026-07-19, per the player): mobs no longer pay units at all since a recent game
        // update -- units come ONLY from coffers, points of interest and floor key items. The old "per kill ~40-110"
        // note here was stale and cost real time: it suggested a per-kill stream like Odyssey's msg-40016, and led to
        // reading msg=372's rising p1 as a running unit total. It is not. There is NO frequent update in Limbus, so
        // the displayed total can only refresh a handful of times per run.
        // The id keys on the WING, SETTLED
        // 2026-07-19 : Apollyon 7247 / Temenos 7239, proven by Apollyon-coffer == Apollyon-point-of-interest == 7247
        // while Temenos-point-of-interest == 7239. So the id can NOT tell a coffer from a point of interest.
        //
        // The SOURCE comes from the packet's target ENTITY instead : bytes 0x18-0x19 are an entity INDEX (0x1A-0x1B
        // are the message id -- reading the four as one u32 is why the first captures came back unresolved), and its
        // name is 'Temenos Coffer #4' for a coffer, '???' for a point of interest. That is the ONLY reliable
        // discriminant : a point of interest grants the Code only ONCE PER WEEK, so the accompanying 7069/7070 item
        // message is absent the rest of the time and cannot be used (verified dead end -- do not rebuild on it).
        if (m == 7247 || m == 7239) {
            // Is this award a real coffer ? Name-based, so a client in another language degrades to "not a coffer"
            // (a missed chip, visible) rather than a false one (silent corruption of the quadrant row).
            char src[28] = { 0 };
            const unsigned tidx = (unsigned)p[0x18] | ((unsigned)p[0x19] << 8);
            entity_name_by_index(tidx, src, sizeof(src));
            bool isCoffer = false;
            for (const char* c = src; *c && !isCoffer; ++c)
                if (((c[0]|32)=='c' && (c[1]|32)=='o' && (c[2]|32)=='f' && (c[3]|32)=='f' && (c[4]|32)=='e' && (c[5]|32)=='r') ||
                    ((c[0]|32)=='c' && (c[1]|32)=='o' && (c[2]|32)=='f' && (c[3]|32)=='f' && (c[4]|32)=='r' && (c[5]|32)=='e'))
                    isCoffer = true;   // "Coffer" (EN) / "Coffre" (FR)
            zt_.limbusUnits = p3; zt_.limbusUnitsCap = p4;
            if (zt_.limbusUnitBase < 0) zt_.limbusUnitBase = p3 - p1;   // total BEFORE this (first-seen) payout
            zt_.limbusRunUnits = (p3 > zt_.limbusUnitBase) ? (p3 - zt_.limbusUnitBase) : 0;
            // A big award whose source we could not name is the one case that silently loses a chip (a client in a
            // language whose coffer is neither "Coffer" nor "Coffre", or an entity slot we failed to read). Say so,
            // rather than letting it look like the coffer simply never happened.
            if (p1 >= LIMBUS_COFFER_MIN && !isCoffer)
                windower::debug::log("LIMBUS: %d-unit award from entity '%s' not recognised as a coffer -> NO chip recorded"
                                     " (point of interest = expected ; otherwise the name test needs this language)",
                                     p1, src[0] ? src : "<unresolved>");
            if (p1 >= LIMBUS_COFFER_MIN && isCoffer) {     // a REAL coffer (named entity), not a point of interest
                zt_.limbusCofferAmt = p1;
                int w = 0; for (; at[w] && w < 11; ++w) zt_.limbusCofferAt[w] = at[w]; zt_.limbusCofferAt[w] = 0;
                const int area = (zt_.curZone == 37) ? 1 : 0;               // 38 Apollyon / 37 Temenos, tracked apart
                LimbusCoffers& lc = lc_[area];
                const int slot = limbus_slot_of(area, zt_.limbusQuad);
                if (p1 >= LIMBUS_BIG_MIN) {                                 // the 5k ends the cycle : the other slots
                    for (int i = 0; i < 4; ++i) lc.prevK[i] = lc.slotK[i];  // archive first -- the threshold is a guess
                    for (int i = 0; i < 4; ++i) lc.slotK[i] = 0;            // clear, but the 5k itself stays GREEN so
                    lc.bigAmt = p1;                                          // the row still shows where it was found
                    int b = 0; for (; zt_.limbusQuad[b] && b < 7; ++b) lc.bigAt[b] = zt_.limbusQuad[b]; lc.bigAt[b] = 0;
                }
                if (slot >= 0) lc.slotK[slot] = (unsigned char)(p1 / 1000);
                lc_save();
            }
            zt_save();
        } else if (m == 7280 || m == 7288) {               // weekly allowance : "you may collect data N more times"
            // 7280 is the CAPTURED id (2026-07-19, from the 'Temenos Operator' entity, p1 = the count). 7288 was the
            // previously assumed value and never matched anything, so this counter was simply never filled. Both are
            // kept : these ids are zone-relative and drift across patches, and the payload shape is identical.
            zt_.limbusWeekLeft = p1;
            // NOTE: no weekly wipe. The row is not a per-week checklist -- it is the last known payout of each
            // quadrant, and it self-corrects because reopening a coffer overwrites its slot (a 5k slot reopened
            // for 3k goes back to red). Only finding a 5k clears the reds. Wiping on a new week would destroy
            // the one thing worth keeping (where the 5k was) without ever being asked to.
            lc_[(zt_.curZone == 37) ? 1 : 0].weekSeen = p1;
            lc_save();
            zt_save();
        }
        return;
    }
    if (zt_.mode != 2) return;
    const int rel = (int)(pkt_u16(p, 0x1A) & 0x3FFF) - zt_.abyOffset;
    const int p1 = (int)pkt_u32(p, 0x08), p2 = (int)pkt_u32(p, 0x0C), p3 = (int)pkt_u32(p, 0x10), p4 = (int)pkt_u32(p, 0x14);
    auto addL = [&](int idx, int add, int cap) { int v = zt_.lights[idx] + add; if (v > cap) v = cap; zt_.lights[idx] = v; };
    bool ch = true;                                        // a matched case changed lights/visitant -> persist (default: no)
    switch (rel) {
        case 0:   zt_.lights[0] = p1; zt_.lights[6] = p2; zt_.lights[4] = p3; zt_.lights[5] = p4; break;   // /heal : pearl,ebon,gold,silver (exact)
        case 1:   zt_.lights[1] = p1; zt_.lights[2] = p2; zt_.lights[3] = p3; break;                       // /heal : azure,ruby,amber (exact)
        case 183: addL(0, 5, 230); break;                                                                 // pearlescent
        case 184: addL(4, 5 * (p1 + 1), 200); break;                                                      // golden
        case 185: addL(5, 5 * (p1 + 1), 200); break;                                                      // silvery
        case 186: addL(6, p1 + 1, 200); break;                                                            // ebon
        case 187: addL(1, 8, 255); break;                                                                 // azure
        case 188: addL(2, 8, 255); break;                                                                 // ruby
        case 189: addL(3, 8, 255); break;                                                                 // amber
        case 9: case 10: case 45: zt_.visitantMin = p1; zt_.visitantMs = GetTickCount(); break;           // update / wears / gain
        case 12:  zt_.visitantMin += p1; zt_.visitantMs = GetTickCount(); break;                          // extend
        default: ch = false; break;
    }
    if (ch) zt_save();
}

} // namespace aio
