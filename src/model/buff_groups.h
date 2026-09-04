// buff_groups.h -- status id -> DISPLAY GROUP, for the party buff strip's configurable order.
//
// WHY a table of our own, next to BUFF_FAM (job_track_gen.h). BUFF_FAM is generated from the game's own
// spell/ability data and is keyed by (status, SOURCE), not by status : 22 statuses appear under two or three
// categories, and the FIRST one listed is not the one a player thinks of --
//     71 -> "Monomi: Ichi" TC_NINJUTSU  before  "Sneak" TC_ENHANCE
//     69 -> "Tonko: Ni"    TC_NINJUTSU  before  "Invisible" TC_ENHANCE
//     33 -> "Haste" TC_HASTE, "Refueling" TC_BLU_BUFF, "Hastega" TC_BPACT
// so a plain "first row wins" lookup files Sneak and Invisible under Ninjutsu. It also splits groups a player
// keeps together (Refresh is TC_REFRESH, Haste is TC_HASTE, Phalanx is TC_DEFENSE with Blink and Stoneskin --
// but they are all "the buffs I watch all the time") and merges ones they separate.
//
// So : an explicit PRIORITY LIST decides the handful of statuses whose group is a judgement call, BUFF_FAM's
// category supplies the rest (it is right about Songs, Rolls, Geomancy, Runes, ... and it is maintained by the
// generator), the debuff list catches what neither knows, and everything left lands in Other.
//
// TWO orders meet in the strip, and they live in different places on purpose :
//   - between groups : the USER's, in UiConfig::buffOrder -- it is a preference, so it is in the config.
//   - inside a group : the priority list below -- "Sneak before Invisible", "Haste, Refresh, Phalanx". The
//     game sends statuses in acquisition order, so this ordering exists nowhere in the data and cannot be
//     derived ; it is a stated design decision, which is why it is code and not a setting.
#pragma once
#include "model/job_track_gen.h"      // BUFF_FAM : status -> TrackCat (the generated classification)
#include "model/action_status_gen.h"  // is_debuff_status
#include "model/buffs_gen.h"          // buff_status_name : a status with no name is not worth a config row
#include "model/ui_config.h"          // the user's arranged prefix per group (ui_config.h includes nothing -- no cycle)

namespace aio {

// Display groups, in their DEFAULT order (position 0 = drawn first = nearest the member's row, i.e. rightmost).
// The user reorders them freely ; this enum only fixes the identities and the factory default.
enum BuffGroup {
    BG_STEALTH,    // Sneak / Invisible / Deodorize -- the ones you want parked at one end and never hunted for
    BG_WATCH,      // Refresh / Haste / Flurry / Regen / Phalanx -- the "is it still up ?" buffs
    BG_PROTECT,    // Protect / Shell / Stoneskin / Blink / Aquaveil / Reraise
    BG_SONG,       // BRD
    BG_ROLL,       // COR
    BG_GEO,        // Indi- / Geo- auras
    BG_RUNE,       // RUN runes + wards
    BG_DANCE,      // DNC sambas + jigs
    BG_ENHANCE,    // enspells, barspells, spikes, stat gains, storms, ninjutsu, BLU self-buffs
    BG_JA,         // job abilities, blood pacts, stratagems, everything ability-shaped
    BG_PERM,       // food, signet/sanction/ionis, aftermath, craft imagery -- the all-day ones
    BG_DEBUFF,     // what the game put on you and you did not want
    BG_OTHER,      // unclassified
    BG_COUNT
};

static const char* const BUFF_GROUP_EN[BG_COUNT] = {
    "Stealth", "Watch", "Protection", "Songs", "Rolls", "Geomancy", "Runes / Wards",
    "Dances", "Enhancing", "Abilities", "Permanent", "Debuffs", "Other"
};
static const char* const BUFF_GROUP_FR[BG_COUNT] = {   // accents spelled as UTF-8 bytes (these sources carry no BOM), like tm_config.cpp does
    "Discr\xC3\xA9tion", "Surveillance", "Protections", "Chants", "Rolls", "G\xC3\xA9omancie", "Runes / Wards",
    "Danses", "Am\xC3\xA9lioration", "Aptitudes", "Permanents", "Debuffs", "Autre"
};
// SHORT labels, for the config strip : the name sits centred over its block, and a block is only as wide as the
// two or three icons it previews. "Surveillance" over 40 pixels would collide with its neighbours, so the strip
// widens a block to fit the SHORT name and uses the full one everywhere else (the selection line, this file).
static const char* const BUFF_GROUP_SHORT_EN[BG_COUNT] = {
    "Stealth", "Watch", "Protect", "Songs", "Rolls", "Geo", "Runes",
    "Dances", "Enhance", "Abils", "Perm", "Debuffs", "Other"
};
static const char* const BUFF_GROUP_SHORT_FR[BG_COUNT] = {
    "Discr", "Surv", "Prot", "Chants", "Rolls", "G\xC3\xA9o", "Runes",
    "Danses", "Am\xC3\xA9lio", "Aptit", "Perm", "Debuffs", "Autre"
};

// ---- the PRIORITY LIST : the statuses whose group is a judgement call, AND whose position inside that
// group is deliberate. It does two jobs at once, on purpose :
//   1. group  -- these win over BUFF_FAM, always (see the header note on Sneak / Invisible).
//   2. ORDER  -- inside one group, entries appear in the order they are LISTED here, ahead of everything
//      else in that group. The game sends a member's statuses in acquisition order, which is arbitrary,
//      so "Sneak then Invisible" or "Haste, Refresh, Phalanx" is not something the data will ever give
//      you -- it has to be stated. Statuses NOT listed here keep the game's own order, after these.
// Reordering two lines in a block below CHANGES what the HUD draws. That is the intent, and it is why
// the exact sequences are pinned by name in tests/t_buffgroups.cpp.
struct BuffGroupFix { unsigned short status; unsigned char group; };
static const BuffGroupFix BUFF_GROUP_FIX[] = {
    // Stealth -- Sneak first : it is the one you re-apply on the move, so it sits nearest the row.
    {  71, BG_STEALTH },   // Sneak      (BUFF_FAM lists "Monomi: Ichi" TC_NINJUTSU first)
    {  69, BG_STEALTH },   // Invisible  (BUFF_FAM lists "Tonko: Ni" TC_NINJUTSU first)
    {  70, BG_STEALTH },   // Deodorize
    // Watch -- the three you actually scan for, in the order you scan them.
    {  33, BG_WATCH   },   // Haste      (also Refueling / Hastega -- same icon, same thing to watch)
    {  43, BG_WATCH   },   // Refresh
    { 116, BG_WATCH   },   // Phalanx    (BUFF_FAM has it in TC_DEFENSE, with Blink and Stoneskin)
    {  42, BG_WATCH   },   // Regen      (not in the stated three -> after them)
    { 581, BG_WATCH   },   // Flurry     (idem)
    // Protection -- Protect before Shell, then the rest of the shielding block.
    {  40, BG_PROTECT },   // Protect
    {  41, BG_PROTECT },   // Shell
    {  37, BG_PROTECT },   // Stoneskin
    {  36, BG_PROTECT },   // Blink
    {  39, BG_PROTECT },   // Aquaveil
    { 113, BG_PROTECT },   // Reraise    (BUFF_FAM has it in TC_HEAL)
};
static const int BUFF_GROUP_FIX_N = (int)(sizeof(BUFF_GROUP_FIX) / sizeof(BUFF_GROUP_FIX[0]));

// ---- one TINT per group. It is the only mark that ties a run of icons in the config strip to the group it
// belongs to, so it needs an origin rather than thirteen hand-picked hues : the first four ARE the HUD's own
// role colours (job_role_color, party_state.cpp) -- tank blue, healer green, buffer gold, DD red -- and the
// rest are chosen to sit in that same muted family. They live in the CONFIG only, never on the HUD, so they
// do not spend the "at most ~2 saturated accents in steady state" budget the design brief sets for in-game.
inline unsigned buff_group_tint(int g) {
    static const unsigned T[BG_COUNT] = {
        0xFF9B8CE0u,   // Stealth     -- violet : the one block you park at an end and never hunt for
        0xFF86D36Fu,   // Watch       -- healer green (job_role_color)
        0xFF7D9BF0u,   // Protection  -- tank blue (job_role_color)
        0xFFECC94Au,   // Songs       -- buffer gold (job_role_color)
        0xFFE0A85Eu,   // Rolls       -- amber, a step off the songs
        0xFF5EC8C0u,   // Geomancy    -- the accent family
        0xFFC87D9Bu,   // Runes/Wards
        0xFFB0D36Fu,   // Dances
        0xFF6FB8E0u,   // Enhancing
        0xFFE08585u,   // Abilities   -- DD red (job_role_color)
        0xFF8C93A0u,   // Permanent   -- deliberately grey : it never changes, it should never draw the eye
        0xFFC4565Fu,   // Debuffs
        0xFF5F6975u    // Other       -- greyest of all
    };
    return (g >= 0 && g < BG_COUNT) ? T[g] : 0xFF5F6975u;
}

// ---- BUFF_FAM category -> group, for everything the fix table does not name. ----
inline unsigned char buff_group_of_cat(int cat) {
    switch (cat) {
        case TC_REFRESH: case TC_HASTE: case TC_REGEN:                      return BG_WATCH;
        case TC_PROTECT: case TC_DEFENSE:                                   return BG_PROTECT;
        case TC_SONG:                                                       return BG_SONG;
        case TC_ROLL:                                                       return BG_ROLL;
        case TC_GEO:                                                        return BG_GEO;
        case TC_RUNE: case TC_WARD:                                         return BG_RUNE;
        case TC_SAMBA: case TC_DANCE:                                       return BG_DANCE;
        case TC_ENSPELL: case TC_BARSPELL: case TC_SPIKES: case TC_GAIN:
        case TC_ENHANCE: case TC_NINJUTSU: case TC_BLU_BUFF:                return BG_ENHANCE;
        case TC_FOOD: case TC_SIGNET: case TC_AFTERMATH: case TC_CRAFT:     return BG_PERM;
        default:                                                            return BG_JA;   // JA, blood pacts, stratagems, summons, utility, the magic schools
    }
}

// Highest status id we resolve. Matches the buff atlas ceiling (32 cols x 20 rows = 640 cells) : an id past
// that is not drawable anyway, and buffs_gen.h stops at 635.
static const int BUFF_GROUP_MAX_ID = 640;

// Two flat byte tables, built ONCE together : the group of a status, and its rank INSIDE that group
// (255 = not on the priority list -> keeps the game's own order, after everything that is listed).
// Flat, because the strip resolves up to 32 ids per member per frame and a binary search per id, per
// member, per frame is the kind of cost that shows up in a profile.
// (A one-shot init with no failure mode -- pure computation over baked tables, no device, no file,
// nothing that can be "not ready yet" -- so it is not the give-up-once-give-up-forever trap of rule 10.)
inline void buff_group_tables(const unsigned char*& grp, const unsigned char*& pri) {
    static unsigned char tblG[BUFF_GROUP_MAX_ID], tblP[BUFF_GROUP_MAX_ID];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < BUFF_GROUP_MAX_ID; ++i) { tblG[i] = BG_OTHER; tblP[i] = 0xFF; }
        for (int i = BUFF_FAM_N - 1; i >= 0; --i)             // reverse : the FIRST row of a duplicated status wins
            if (BUFF_FAM[i].status < BUFF_GROUP_MAX_ID) tblG[BUFF_FAM[i].status] = buff_group_of_cat(BUFF_FAM[i].cat);
        for (int i = 0; i < BUFF_GROUP_MAX_ID; ++i)           // what neither table knows, but the game marks as a debuff
            if (tblG[i] == BG_OTHER && is_debuff_status((unsigned)i)) tblG[i] = BG_DEBUFF;
        unsigned char next[BG_COUNT] = { 0 };                 // running rank per group -> "listed order" becomes "draw order"
        for (int i = 0; i < BUFF_GROUP_FIX_N; ++i) {          // the priority list, last : it wins the group AND sets the rank
            const unsigned st = BUFF_GROUP_FIX[i].status, g = BUFF_GROUP_FIX[i].group;
            if (st >= (unsigned)BUFF_GROUP_MAX_ID || g >= BG_COUNT) continue;
            tblG[st] = (unsigned char)g;
            tblP[st] = next[g]++;
        }
        built = true;
    }
    grp = tblG; pri = tblP;
}
inline unsigned char buff_group(unsigned status) {
    const unsigned char *g, *p; buff_group_tables(g, p);
    return (status < (unsigned)BUFF_GROUP_MAX_ID) ? g[status] : (unsigned char)BG_OTHER;
}
// Rank inside the group : lower draws first. 255 = unlisted -> after every listed status, in game order.
inline unsigned char buff_group_pri(unsigned status) {
    const unsigned char *g, *p; buff_group_tables(g, p);
    return (status < (unsigned)BUFF_GROUP_MAX_ID) ? p[status] : (unsigned char)0xFF;
}

// ---- the EFFECTIVE order inside a group : the user's arrangement if there is one, else the built-in list.
// A group the user has arranged ignores the built-in ranks ENTIRELY -- including for statuses the user never
// touched. That is not a loss : the UI seeds the prefix from the order being displayed, so the first move
// preserves everything above it and the two rules can never disagree about what is on screen.
inline unsigned char buff_pri_effective(const UiConfig& c, unsigned status) {
    const int g = buff_group(status);
    if (g >= 0 && g < UiConfig::BUFF_ORDER_N && c.buffPinN[g] > 0) {
        const int r = c.buff_pin_rank(g, status);
        return (r >= 0) ? (unsigned char)r : (unsigned char)0xFF;
    }
    return buff_group_pri(status);
}

// The statuses of group `g` worth OFFERING to arrange, in the order the strip would draw them.
//
// Not "every status of the group" : "Other" holds ~238 named ones and "Abilities" 134, and a menu that lists
// them all to be hand-ordered is a menu nobody opens. A status earns a row when it is one the user could
// plausibly want moved --
//   - it carries a built-in rank (the curated list : Sneak, Haste, Protect...), so the groups that matter are
//     complete from the first launch, before anything has been seen ; or
//   - the user has arranged it ; or
//   - `seen` says it has actually turned up on somebody this session.
// `seen` may be null (no session data -- the tests, a preview) : the list is then the curated one.
// Unnamed ids never qualify : a row reading "471" orders nothing a player can recognise.
// Ordered by effective rank, then status id -- the id being the closest stand-in for "the order the game
// sends them" that a static list can show. Returns the count written ; `total` gets the number that
// qualified, so the caller can say how many it had to leave out.
inline int buff_group_members(const UiConfig& c, int g, unsigned short* out, int cap, int* total,
                              bool (*seen)(unsigned)) {
    int n = 0, qualified = 0;
    for (unsigned id = 0; id < (unsigned)BUFF_GROUP_MAX_ID; ++id) {
        if (buff_group(id) != g || !buff_status_name(id)) continue;
        const unsigned char pr = buff_pri_effective(c, id);
        if (pr == 0xFF && buff_group_pri(id) == 0xFF && !(seen && seen(id))) continue;
        ++qualified;
        // BOUNDED insertion sort. The cap must drop the WORST-ranked members, not the highest ids : a status
        // the user pinned to position 1 can have any id at all, and a cap that kept "the first `cap` ids"
        // would drop it while keeping unarranged ones. So a full list evicts its own last entry instead.
        int q;
        if (n < cap) q = n++;
        else if (pr >= buff_pri_effective(c, out[cap - 1])) continue;   // no better than the worst we keep (>= : ties keep the lower id, already in)
        else q = cap - 1;                                               // evict it
        for (; q > 0 && buff_pri_effective(c, out[q - 1]) > pr; --q)    // strict > : equal ranks keep ascending id order,
            out[q] = out[q - 1];                                        // which is the order this loop walks them in
        out[q] = (unsigned short)id;
    }
    if (total) *total = qualified;
    return n;
}

} // namespace aio
