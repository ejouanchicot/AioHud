// omen_objectives.h -- the Omen (Reisenjima Henge) bonus-objective text protocol, as PURE functions.
//
// Everything here is decided from a chat line and a 10-slot array : no game memory, no packets, no clock, no
// file. That is deliberate. The bug this file was extracted for -- objectives from the PREVIOUS floor sitting in
// the box forever, unable to validate -- is a state-machine bug across a floor boundary, and a floor boundary
// costs a real Omen run to reach. Kept pure, the whole sequence replays offline in tests/t_omen.cpp against the
// transcript actually captured in game (//aio omenparse, 2026-07-29).
//
// The text patterns are ported from the Omen addon (Braden/Sechs, addons/Omen/Omen.lua) and then CONFIRMED
// line by line against that capture. The addon matches with Lua patterns; here each objective type is found by
// a distinctive substring, and its number by the "check letter" -- the first digit run followed by a word
// starting with that letter, which is exactly what the addon's `check="%d+%su"` patterns do. The leading "N:"
// never matches because a colon follows it.
#pragma once
#include <string.h>

namespace aio {

// One bonus objective slot. req = -1 means "the statement was never seen" (loaded mid-floor) -> the box shows
// "???" rather than a number it does not have, and the row cannot be called complete.
struct OmenObj { int type = 0; int cur = 0; int req = 0; };

// What a single "N: ..." line said.
struct OmenLine {
    int  slot   = 0;      // 1..10 ; 0 = not an objective line at all
    int  type   = 0;      // 1..14 (see omen_short_name) ; 0 = no pattern matched
    int  num    = -1;     // the number the line carries ; -1 = none found
    char letter = 0;      // the check letter that located it (diagnostics)
    bool eval   = false;  // progress report ("You have used 4 weapon skills") vs. the statement ("Use 10 ...")
    bool fail   = false;  // "You have failed to ..." -- the window closed on this one
};

inline const char* omen_short_name(int t) {
    static const char* N[15] = { "", "WS Damage", "MB Damage", "Non-MB Nuke", "Melee Round", "Kills",
        "Critical Hits", "Abilities", "Spells", "Magic Bursts", "Skillchains", "All WS", "Physical WS",
        "Magic WS", "500 HP Cures" };
    return (t >= 1 && t <= 14) ? N[t] : "";
}

// The number immediately preceding a word starting with `letter`. Skips the leading "N:" for free (a colon is
// not a letter) and skips "500 HP" when hunting the 't' of "10 times".
inline int omen_num_before(const char* s, char letter) {
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

inline int omen_first_num(const char* s) {
    for (int i = 0; s[i]; ++i)
        if (s[i] >= '0' && s[i] <= '9') { long v = 0; while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; } return (int)v; }
    return -1;
}

inline OmenLine omen_parse_line(const char* s) {
    OmenLine L;
    if (!s || !(s[0] >= '1' && s[0] <= '9')) return L;
    int slot = 0, i = 0;
    while (s[i] >= '0' && s[i] <= '9') { slot = slot * 10 + (s[i] - '0'); ++i; }
    if (s[i] != ':' || slot < 1 || slot > 10) return L;          // not "N: ..." -> not ours
    L.slot = slot;
    if (strstr(s, "You have failed")) { L.fail = true; return L; }
    L.eval = strstr(s, "You have") != 0;
    // Distinctive tails, SPECIFIC FIRST : "single weapon skill" must be tested before "weapon skill", and
    // "without performing a magic burst" before "magic burst", or a damage objective is filed as a count one.
    struct T { const char* sub; int id; char c; };
    static const T TT[] = {
        { "without performing a magic burst", 3, 'u' }, { "single magic burst", 2, 'u' }, { "single weapon skill", 1, 'u' },
        { "single auto-attack", 4, 'i' }, { "killchain", 10, 's' }, { "elemental weapon", 13, 'e' }, { "physical weapon", 12, 'p' },
        { "weapon skill", 11, 'w' }, { "critical", 6, 'c' }, { "magic burst", 9, 'm' }, { "anquish", 5, 'f' },
        { "500 HP", 14, 't' }, { "pell", 8, 's' }, { "bilit", 7, 'a' } };
    for (unsigned k = 0; k < sizeof(TT) / sizeof(TT[0]); ++k)
        if (strstr(s, TT[k].sub)) { L.type = TT[k].id; L.letter = TT[k].c; break; }
    if (L.type) L.num = omen_num_before(s, L.letter);
    return L;
}

// Fold a parsed line into the slot array.
//
// A progress line sets the COUNT ONLY. It must never touch req : for a damage objective the game reports what
// you actually dealt ("You have reduced your foe's HP by 455 in a single auto-attack." against a 2000 target),
// so taking its number as the requirement would declare every such objective complete on the first hit.
inline void omen_apply_line(OmenObj* objs, const OmenLine& L) {
    if (!objs || L.slot < 1 || L.slot > 10 || !L.type || L.fail) return;
    OmenObj& o = objs[L.slot - 1];
    if (L.eval) {
        if (L.num >= 0) o.cur = L.num;
        if (o.type == 0) { o.type = L.type; o.req = -1; }        // joined mid-floor : count known, target not
    } else {
        if (o.type != L.type) o.cur = 0;                          // a DIFFERENT objective now occupies this slot
        o.type = L.type;
        if (L.num >= 0) o.req = L.num;
    }
}

inline void omen_reset(OmenObj* objs) { if (objs) for (int k = 0; k < 10; ++k) objs[k] = OmenObj{}; }

// Complete. req must be positive : 0 means never stated and -1 means unknown, and neither can be "reached".
inline bool omen_done(const OmenObj& o) { return o.req > 0 && o.cur >= o.req; }

// "You have N seconds remaining." -- SINGULAR at the last tick ("1 second remaining"), which a "seconds"-only
// test silently dropped.
inline bool omen_is_timer_line(const char* s) { return s && strstr(s, "second") && strstr(s, "remaining") != 0; }

// The bonus window OPENING, which is the one reliable "a new floor's objectives are about to be listed" marker.
//
// Every later tick of the same window drops the prefix ("Carry out your charge before time runs out.You have 60
// seconds remaining."), and the periodic flavour line "Obey... Follow the light..." shares only the first three
// words -- hence matching the whole phrase. The alternative marker, "A spectral light flares up.", fires only on
// a floor you FULLY CLEAR, so hanging the reset on it left every abandoned floor's objectives in place.
inline bool omen_is_new_floor_line(const char* s) {
    return omen_is_timer_line(s) && strstr(s, "Follow the light and carry out your charge") != 0;
}

// The floor's own objective banner ("Vanquish all transcended foes.", "Open 3 treasure portents", or the
// free-floor "The light shall come even if you fail to obey."). RE-BROADCAST periodically during a floor, so on
// its own it says nothing about a floor CHANGE -- it only means one when a clear was seen just before it.
inline bool omen_is_floor_banner_line(const char* s) {
    return s && (strstr(s, "Vanquish") || strstr(s, "treasure portent") || strstr(s, "light shall come even if you fail"));
}

// Does THIS floor carry bonus objectives at all?
//
// Taken from the reference addon's `hide_timer` list (Omen.lua), which we had not ported: the six Omen bosses
// (Kin, Gin, Kei, Kyou, Fu, Ou), the three mid-bosses (Craver, Gorger, Thinker), the treasure-portent floors and
// the pre-run "Waiting for objectives..." state have NO bonus objectives, and the addon hides the whole bonus
// body on them. Without this the box announces a bonus window and a "Bonus 0:00" countdown on a floor that has
// none -- and, before the wipe fix, showed the previous floor's rows there, which is the loudest form the
// reported bug took.
//
// Matched as WHOLE WORDS, unlike the addon's plain substring test: "Fu" and "Ou" are two letters, and a
// substring rule on tokens that short is one unlucky monster name away from blanking a normal floor's
// objectives -- a silent, hard-to-attribute failure.
inline bool omen_floor_has_bonus(const char* banner) {
    if (!banner || !banner[0]) return false;                      // nothing known yet -> nothing to promise
    static const char* NOBONUS[] = { "Kin", "Gin", "Kei", "Kyou", "Fu", "Ou",
                                     "Craver", "Gorger", "Thinker", "Treasure", "Waiting" };
    for (unsigned k = 0; k < sizeof(NOBONUS) / sizeof(NOBONUS[0]); ++k) {
        const char* t = NOBONUS[k];
        const int n = (int)strlen(t);
        for (const char* p = strstr(banner, t); p; p = strstr(p + 1, t)) {
            const char before = (p == banner) ? ' ' : p[-1];
            const char after  = p[n];
            const bool bAlpha = (before >= 'A' && before <= 'Z') || (before >= 'a' && before <= 'z');
            const bool aAlpha = (after  >= 'A' && after  <= 'Z') || (after  >= 'a' && after  <= 'z');
            if (!bAlpha && !aAlpha) return false;
        }
    }
    return true;
}

// EVERY effect a chat line can have on the ten slots, in one place. `floorCleared` is the "A spectral light
// flares up." latch. Returns true when the slots were wiped.
//
// One entry point on purpose. The reported bug was not a parsing mistake -- the parser was already correct on
// every line of the capture. It was that the WIPE lived somewhere else than the parse, behind a condition
// ("you fully cleared the floor") that a real run does not always meet. Two ways to reach one array is how that
// happens; a caller that cannot forget to wipe is the fix.
inline bool omen_feed_slots(OmenObj* objs, const char* s, bool floorCleared) {
    if (!objs || !s) return false;
    if (omen_is_new_floor_line(s)) { omen_reset(objs); return true; }        // the bonus window opening
    if (floorCleared && omen_is_floor_banner_line(s)) { omen_reset(objs); return true; }   // second chance, after a clear
    omen_apply_line(objs, omen_parse_line(s));
    return false;
}

// Printable copy of a chat line for display. 0x7F is a MARKER byte followed by one code byte : dropping only the
// marker left the code behind, and every Omen line ends with 0x7F 0x31 -- so the floor banner read
// "Vanquish all monsters.1".
inline void omen_clean_text(const char* s, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!s) return;
    int w = 0;
    for (int i = 0; s[i] && w < cap - 1; ++i) {
        const char c = s[i];
        if (c == 0x7F) { if (s[i + 1]) ++i; continue; }
        if (c >= 0x20 && c < 0x7F) out[w++] = c;
    }
    while (w > 0 && out[w - 1] == ' ') --w;
    out[w] = 0;
}

} // namespace aio
