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

// ---- the statuses BUFF_FAM never sees at all. ----
// BUFF_FAM is generated from spells and job abilities that CARRY a status, so it only knows a status the game
// grants through something castable. Everything else -- a shadow count, a stat penalty, a daze, an avatar's
// favour, a stratagem, a maneuver -- lands in Other by default, and 238 named statuses ended up there. Most of
// them plainly belong somewhere: Hide and Camouflage are stealth, Copy Image is what Utsusemi leaves behind,
// Finishing Moves are DNC's, the Dazes are debuffs by any reading.
//
// Written as RANGES rather than 150 rows because that is what they are -- the game numbers a family
// consecutively -- and because a range states the intent where a list of ids states nothing. Applied ONLY to a
// status still unclassified, so it can never overrule the generated table or the debuff list; the priority
// list above still wins over everything.
struct BuffGroupRange { unsigned short lo, hi; unsigned char group; const char* what; };
static const BuffGroupRange BUFF_GROUP_RANGE[] = {
    {  76,  77, BG_STEALTH, "Hide, Camouflage" },
    {  66,  66, BG_ENHANCE, "Copy Image (Utsusemi)" },
    { 444, 446, BG_ENHANCE, "Copy Image 2/3/4+" },
    {  80,  85, BG_ENHANCE, "STR..MND Boost" },
    {  89,  90, BG_ENHANCE, "Max MP / Accuracy Boost" },
    { 125, 125, BG_ENHANCE, "CHR Boost" },
    { 611, 611, BG_ENHANCE, "Magic Evasion Boost" },
    { 622, 622, BG_ENHANCE, "Guarding Rate Boost" },
    { 615, 615, BG_ENHANCE, "Boost (MNK)" },
    { 277, 282, BG_ENHANCE, "Enspell II" },
    { 487, 488, BG_ENHANCE, "Endrain, Enaspir" },
    { 589, 596, BG_ENHANCE, "the storms" },
    { 153, 153, BG_ENHANCE, "Damage Spikes" },
    { 573, 573, BG_ENHANCE, "Deluge Spikes" },
    { 605, 607, BG_ENHANCE, "Gale / Clod / Glint Spikes" },
    { 574, 574, BG_ENHANCE, "Fast Cast" },
    { 188, 188, BG_ENHANCE, "Sublimation: Complete" },
    { 161, 161, BG_ENHANCE, "Sprint" },
    { 162, 162, BG_ENHANCE, "Enchantment" },
    // (233 Auto-Regen and 234 Auto-Refresh are deliberately NOT here. They are JOB TRAITS : the resource table
    //  names them, but they never appear in anyone's buff list, so classifying them only put two rows in a
    //  group that can never show them. Unclassified is the honest place for a status that never arrives --
    //  a name in the data is not evidence that the game ever sends it.)
    { 265, 265, BG_WATCH,   "Flurry (the second one)" },
    { 151, 151, BG_PROTECT, "Arrow Shield" },
    { 293, 297, BG_PROTECT, "Negate Petrify..Poison" },
    { 608, 610, BG_PROTECT, "Negate Virus / Curse / Charm" },
    { 626, 626, BG_PROTECT, "Negate Sleep" },
    { 193, 193, BG_SONG,    "Lullaby" },
    { 204, 204, BG_SONG,    "Hum" },
    { 208, 208, BG_SONG,    "Serenade" },
    { 211, 212, BG_SONG,    "Fugue, Rhapsody" },
    { 299, 307, BG_JA,      "Overload + the eight maneuvers (PUP)" },
    { 360, 367, BG_JA,      "Penury..Manifestation (SCH stratagems)" },
    { 412, 415, BG_JA,      "Altruism..Equanimity (SCH)" },
    { 469, 470, BG_JA,      "Perpetuance, Immanence (SCH)" },
    { 516, 516, BG_JA,      "Ecliptic Attrition (SMN)" },
    { 519, 519, BG_JA,      "Theurgic Focus (SCH)" },
    { 381, 385, BG_JA,      "Finishing Moves 1-5 (DNC)" },
    { 588, 588, BG_JA,      "Finishing Move 6+" },
    { 308, 309, BG_JA,      "Double-Up Chance, Bust (COR)" },
    { 422, 431, BG_JA,      "the avatars' Favor (SMN)" },
    { 577, 577, BG_JA,      "Cait Sith's Favor" },
    { 625, 625, BG_JA,      "Siren's Favor" },
    { 408, 408, BG_JA,      "Sekkanoki (SAM)" },
    { 456, 456, BG_JA,      "Spur" },
    { 459, 459, BG_JA,      "Divine Caress (WHM)" },
    { 463, 466, BG_JA,      "Sepulcher..Dragon Breaker" },
    { 496, 496, BG_JA,      "Intervene (PLD)" },
    { 506, 506, BG_JA,      "Grace" },
    { 536, 536, BG_JA,      "Gambit (RUN)" },
    { 538, 538, BG_JA,      "One For All (RUN)" },
    { 571, 571, BG_JA,      "Rayke (RUN)" },
    { 623, 623, BG_JA,      "Rampart (PLD)" },
    { 273, 273, BG_PERM,    "Aftermath" },
    { 489, 489, BG_PERM,    "Afterglow" },
    { 249, 250, BG_PERM,    "Dedication, EF Badge" },
    { 267, 267, BG_PERM,    "Allied Tags" },
    { 269, 269, BG_PERM,    "Level Sync" },
    { 602, 603, BG_PERM,    "Vorseal, Elvorseal" },
    { 616, 616, BG_PERM,    "Artisanal Knowledge" },
    { 136, 149, BG_DEBUFF,  "STR..CHR Down, Level Restriction, Max HP/MP Down, Accuracy Down" },
    { 156, 160, BG_DEBUFF,  "Flash, SJ Restriction, Provoke, Penalty, Preparations" },
    { 167, 167, BG_DEBUFF,  "Magic Def. Down" },
    { 174, 175, BG_DEBUFF,  "Magic Acc. / Atk. Down" },
    { 177, 177, BG_DEBUFF,  "Encumbrance" },
    { 189, 189, BG_DEBUFF,  "Max TP Down" },
    { 259, 264, BG_DEBUFF,  "Encumbrance..Pathos" },
    { 291, 291, BG_DEBUFF,  "Enmity Down" },
    { 298, 298, BG_DEBUFF,  "Critical Hit Evasion Down" },
    { 378, 380, BG_DEBUFF,  "Drain / Aspir / Haste Daze" },
    { 386, 400, BG_DEBUFF,  "Lethargic / Sluggish / Weakened Daze" },
    { 448, 452, BG_DEBUFF,  "Bewildered Daze" },
    { 473, 473, BG_DEBUFF,  "Muddle" },
    { 509, 509, BG_DEBUFF,  "Odyllic Subterfuge" },
    { 572, 572, BG_DEBUFF,  "Avoidance Down" },
    { 576, 576, BG_DEBUFF,  "Doubt" },
    { 630, 631, BG_DEBUFF,  "Taint, Haunt" },
    {  11,  11, BG_DEBUFF,  "Bind" },
};
static const int BUFF_GROUP_RANGE_N = (int)(sizeof(BUFF_GROUP_RANGE) / sizeof(BUFF_GROUP_RANGE[0]));

// Highest status id we resolve. Matches the buff atlas ceiling (32 cols x 20 rows = 640 cells) : an id past
// that is not drawable anyway, and buffs_gen.h stops at 635.
static const int BUFF_GROUP_MAX_ID = 640;

// Two flat byte tables, built ONCE together : the group of a status, and its rank INSIDE that group
// (255 = not on the priority list -> keeps the game's own order, after everything that is listed).
// Flat, because the strip resolves up to 32 ids per member per frame and a binary search per id, per
// member, per frame is the kind of cost that shows up in a profile.
// (A one-shot init with no failure mode -- pure computation over baked tables, no device, no file,
// nothing that can be "not ready yet" -- so it is not the give-up-once-give-up-forever trap of rule 10.)
inline void buff_group_tables(const unsigned char*& grp, const unsigned char*& pri, const unsigned short** canon = 0) {
    static unsigned char tblG[BUFF_GROUP_MAX_ID], tblP[BUFF_GROUP_MAX_ID];
    static unsigned short tblC[BUFF_GROUP_MAX_ID];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < BUFF_GROUP_MAX_ID; ++i) { tblG[i] = BG_OTHER; tblP[i] = 0xFF; }
        for (int i = BUFF_FAM_N - 1; i >= 0; --i)             // reverse : the FIRST row of a duplicated status wins
            if (BUFF_FAM[i].status < BUFF_GROUP_MAX_ID) tblG[BUFF_FAM[i].status] = buff_group_of_cat(BUFF_FAM[i].cat);
        for (int i = 0; i < BUFF_GROUP_MAX_ID; ++i)           // what neither table knows, but the game marks as a debuff
            if (tblG[i] == BG_OTHER && is_debuff_status((unsigned)i)) tblG[i] = BG_DEBUFF;
        for (int r = 0; r < BUFF_GROUP_RANGE_N; ++r)          // the families the generator never sees ; ONLY over what is still unclassified
            for (int i = BUFF_GROUP_RANGE[r].lo; i <= BUFF_GROUP_RANGE[r].hi && i < BUFF_GROUP_MAX_ID; ++i)
                if (tblG[i] == BG_OTHER) tblG[i] = BUFF_GROUP_RANGE[r].group;
        for (int i = 0; i < BUFF_GROUP_FIX_N; ++i) {          // the priority list wins the GROUP ; its rank waits for canon
            const unsigned st = BUFF_GROUP_FIX[i].status, g = BUFF_GROUP_FIX[i].group;
            if (st < (unsigned)BUFF_GROUP_MAX_ID && g < BG_COUNT) tblG[st] = (unsigned char)g;
        }
        // ---- ONE ENTRY PER NAME, per group. The game gives the same buff several status ids depending on
        // where it came from : Flurry is 265 and 581, STR Boost is 80, 119 and 542, 46 names in all. You never
        // carry two of them at once -- they are the same effect -- so showing two tiles, ordering them twice
        // and hiding them separately is asking the user to maintain a distinction the game does not make.
        // Each id maps to the LOWEST id sharing its name AND its group ; order and visibility then hang off
        // that one, and every variant follows. Same group is the essential half: Haste is 33 in Watch and 580
        // as a GEO aura, and those are genuinely different rows on the HUD.
        for (int i = 0; i < BUFF_GROUP_MAX_ID; ++i) {
            tblC[i] = (unsigned short)i;
            const char* ni = buff_status_name((unsigned)i);
            if (!ni) continue;
            for (int j = 0; j < i; ++j) {
                if (tblG[j] != tblG[i]) continue;
                const char* nj = buff_status_name((unsigned)j);
                if (!nj) continue;
                bool same = true;
                for (int k = 0; ; ++k) { if (ni[k] != nj[k]) { same = false; break; } if (!ni[k]) break; }
                if (same) { tblC[i] = tblC[j]; break; }
            }
        }
        // The RANK is written on the CANONICAL id, and only now, because canon needs the final groups. The
        // priority list names 581 for Flurry while the canonical Flurry is 265 (the lower of the two ids the
        // game gives the same buff) -- writing the rank on 581 left the entry the editor actually lists with
        // no rank at all, and Flurry fell out of Watch's order entirely. The list names an EFFECT ; the rank
        // has to land on whichever id represents it.
        unsigned char next[BG_COUNT] = { 0 };
        for (int i = 0; i < BUFF_GROUP_FIX_N; ++i) {
            const unsigned st = BUFF_GROUP_FIX[i].status, g = BUFF_GROUP_FIX[i].group;
            if (st >= (unsigned)BUFF_GROUP_MAX_ID || g >= BG_COUNT) continue;
            const unsigned short cid = tblC[st];
            if (tblP[cid] == 0xFF) tblP[cid] = next[g]++;   // once per effect, in the order the list states
        }
        built = true;
    }
    grp = tblG; pri = tblP; if (canon) *canon = tblC;
}
inline unsigned char buff_group(unsigned status) {
    const unsigned char *g, *p; buff_group_tables(g, p);
    return (status < (unsigned)BUFF_GROUP_MAX_ID) ? g[status] : (unsigned char)BG_OTHER;
}
// The id that REPRESENTS a status : itself, or the lowest id sharing its name inside the same group. Order,
// visibility and the editor all key on this, so the several ids of one effect behave as the one effect they are.
inline unsigned short buff_canon(unsigned status) {
    const unsigned char *g, *p; const unsigned short* c = 0; buff_group_tables(g, p, &c);
    return (status < (unsigned)BUFF_GROUP_MAX_ID && c) ? c[status] : (unsigned short)status;
}
// Hidden, resolved through the canonical id : hiding "Flurry" hides every id the game calls Flurry in that group.
inline bool buff_hidden_effective(const UiConfig& cfg, unsigned status) {
    return cfg.buff_status_hidden(buff_canon(status));
}
// Rank inside the group : lower draws first. 255 = unlisted -> after every listed status, in game order.
inline unsigned char buff_group_pri(unsigned status) {
    const unsigned char *g, *p; const unsigned short* c = 0; buff_group_tables(g, p, &c);
    if (status >= (unsigned)BUFF_GROUP_MAX_ID) return 0xFF;
    return p[c ? c[status] : status];   // any id of an effect answers with the effect's rank
}

// ---- the EFFECTIVE order inside a group : the user's arrangement if there is one, else the built-in list.
// A group the user has arranged ignores the built-in ranks ENTIRELY -- including for statuses the user never
// touched. That is not a loss : the UI seeds the prefix from the order being displayed, so the first move
// preserves everything above it and the two rules can never disagree about what is on screen.
inline unsigned char buff_pri_effective(const UiConfig& c, unsigned status) {
    status = buff_canon(status);   // every id of one effect shares its place in the order
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
        if (buff_canon(id) != id) continue;   // a second id for an effect already listed -- one tile, not two
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
