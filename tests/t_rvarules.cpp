// t_rvarules.cpp -- the decisions of the FFXiMain static-address healers (model/rva_rules.h), one section per rule.
//
// ffximain_rva.cpp had fifteen fix commits and no test. Each section below is one of those fixes, named in its
// comment, with the happy path beside the failure it corrected. Every "Mutation :" line was put back into the rule
// and verified to fail THAT section (dev/scripts/unit_mutate.py). Some mutants look contorted on purpose : the suite
// builds with /W4 /WX, and a mutant that stops using a parameter fails to COMPILE, which proves nothing.
#include "check.h"
#include "model/rva_rules.h"
#include "model/flipwatch.h"
#include <cstring>

using namespace aio;

namespace {
unsigned tag4(const char* s) { return (unsigned)(unsigned char)s[0] | ((unsigned)(unsigned char)s[1] << 8)
                                    | ((unsigned)(unsigned char)s[2] << 16) | ((unsigned)(unsigned char)s[3] << 24); }

// The registry as the cache writer reads it.
struct Reg { unsigned r[FM_N]; bool c[FM_N]; const char* how[FM_N]; };
struct RegSrc {
    const Reg& g;
    unsigned    rva(int i) const       { return g.r[i]; }
    bool        confirmed(int i) const { return g.c[i]; }
    const char* name(int) const        { return "static"; }
    const char* how(int i) const       { return g.how[i] ? g.how[i] : ""; }
};

// Action tables : a fake that names every id it is asked about, so the RULE's ranges are what is being tested.
struct AnyAction {
    bool spell(unsigned) const { return true; }
    bool abil(unsigned) const  { return true; }
    bool ws(unsigned) const    { return true; }
};
// ...and one that names only what the client really had on 2026-09-11 : weapon skill 3, job ability 19 (Manafont).
struct FewActions {
    bool spell(unsigned id) const { return id == 0x189; }
    bool abil(unsigned id) const  { return id == 19; }
    bool ws(unsigned id) const    { return id == 3; }
};
} // namespace

void test_rva_rules() {

    // ---------------------------------------------------------------- the disk cache ----
    const unsigned FP = 0x6A580428u;

    SECTION("rva cache : a file written for another client is ignored, never trusted");
    {   // 725c8d4 : the whole safety of the cache is the fingerprint. A stale address read as live data is the one
        // failure worse than a dead feature.
        // Mutation : r.fp = v; fpOk = (v == fpNow); -> r.fp = v; fpOk = (v == fpNow || v != fpNow);
        RvaCacheResult r;
        rva_cache_parse("format=2\nfingerprint=11111111\nrva1=62188C\nrva2=667A48\n", 2, FP, r);
        CHECK_EQ(RVA_CACHE_BAD_CLIENT, r.status);
        CHECK_EQ(0, r.n);
        CHECK(!r.got[1] && !r.got[2]);
        CHECK_EQ(0x11111111u, r.fp);                       // named, for the log line
        rva_cache_parse("format=2\nfingerprint=6A580428\nrva1=62188C\nrva2=667A48\n", 2, FP, r);
        CHECK_EQ(RVA_CACHE_OK, r.status);
        CHECK_EQ(2, r.n);
        CHECK_EQ(0x62188Cu, r.rva[1]);
        CHECK(r.got[1] && r.got[2] && !r.got[0]);
    }

    SECTION("rva cache : another format, or no format at all, is ignored");
    {   // 725c8d4 : a wrong address adopted by an older, weaker rule must not come back from disk "confirmed" --
        // the first menu healer's log-window slot. A file with no format= line is pre-versioned.
        // Mutation : if (!fpOk || !fmtOk) continue; -> if (!fpOk) continue;
        RvaCacheResult r;
        rva_cache_parse("fingerprint=6A580428\nrva1=621890\n", 2, FP, r);
        CHECK_EQ(RVA_CACHE_OK, r.status);
        CHECK_EQ(0, r.n);
        CHECK(!r.got[1]);
        rva_cache_parse("format=1\nfingerprint=6A580428\nrva1=621890\n", 2, FP, r);
        CHECK_EQ(RVA_CACHE_BAD_FORMAT, r.status);
        CHECK_EQ(1, r.fmt);
        CHECK(!r.got[1]);
        // and junk entries never count : an index out of range, a zero address, a comment
        rva_cache_parse("format=2\nfingerprint=6A580428\nrva9=1234\nrva0=0\n#rva3=6670B0\nrva-1=5\n", 2, FP, r);
        CHECK_EQ(0, r.n);
    }

    SECTION("rva cache : only PROVEN addresses are written, and exactly those come back");
    {   // 725c8d4 : a proposal is a working hypothesis ; persisting it promotes it to fact on the next login.
        // Mutation : if (s.confirmed(i)) n += -> if (s.rva(i)) n +=
        Reg g; memset(&g, 0, sizeof(g));
        const unsigned R[FM_N] = { 0x57876C, 0x62188C, 0x667A48, 0x6670B0, 0x485644, 0x485826 };
        for (int i = 0; i < FM_N; ++i) g.r[i] = R[i];
        g.c[FM_TARGET_T] = g.c[FM_MENU_PTR] = g.c[FM_PW_BLOCK] = true;      // the other three are proposals
        g.how[FM_MENU_PTR] = "two different menu names on one slot";
        char buf[2048];
        const int len = rva_cache_write(buf, sizeof(buf), 2, FP, RegSrc{ g });
        CHECK(len > 0 && len < (int)sizeof(buf));
        CHECK(strstr(buf, "format=2\nfingerprint=6A580428\n") != 0);
        CHECK(strstr(buf, "rva1=62188C  # static (two different menu names on one slot)\n") != 0);
        CHECK(strstr(buf, "rva2=") == 0);
        RvaCacheResult r;
        rva_cache_parse(buf, 2, FP, r);
        CHECK_EQ(RVA_CACHE_OK, r.status);
        CHECK_EQ(3, r.n);
        for (int i = 0; i < FM_N; ++i) {
            CHECK_EQ(g.c[i], r.got[i]);
            if (r.got[i]) CHECK_EQ(R[i], r.rva[i]);
        }
        // the same bytes on the next client are discarded whole
        rva_cache_parse(buf, 2, FP + 1, r);
        CHECK_EQ(0, r.n);
        // a buffer too small truncates, terminated, and never claims more than it holds
        char tiny[40];
        const int tl = rva_cache_write(tiny, sizeof(tiny), 2, FP, RegSrc{ g });
        CHECK_EQ((int)strlen(tiny), tl);
    }

    // ---------------------------------------------------------------- adoption ----

    SECTION("rva adopt : re-adopting what is already known says nothing");
    {   // Several callers re-adopt every frame their proof holds ; each non-noop adoption logs and rewrites the cache.
        // Mutation : (confirmedNow || !confirmedArg) -> (confirmedNow || (confirmedArg && !confirmedArg))
        CHECK_EQ(RVA_ADOPT_NOOP,    rva_adopt_verdict(0x100, 0x100, true,  true,  true));    // proven, re-proven
        CHECK_EQ(RVA_ADOPT_NOOP,    rva_adopt_verdict(0x100, 0x100, true,  true,  false));   // proven, re-proposed
        CHECK_EQ(RVA_ADOPT_NOOP,    rva_adopt_verdict(0x100, 0x100, true,  false, false));   // proposed, re-proposed
        CHECK_EQ(RVA_ADOPT_CONFIRM, rva_adopt_verdict(0x100, 0x100, true,  false, true));    // the upgrade
        CHECK_EQ(RVA_ADOPT_CONFIRM, rva_adopt_verdict(0x100, 0x100, false, false, false));   // a seed, first heard of
        CHECK_EQ(RVA_ADOPT_MOVE,    rva_adopt_verdict(0x100, 0x104, true,  true,  true));
    }

    // ---------------------------------------------------------------- the menu pointer ----

    SECTION("rva names : the tags are the four bytes the game writes");
    {   // Mutation : RVA_NAME_ABIL   = 0x6C696261u -> RVA_NAME_ABIL   = 0x6C696262u
        CHECK_EQ(tag4("menu"), RVA_TAG_MENU);
        CHECK_EQ(tag4("inline"), RVA_NAME_INLINE);
        CHECK_EQ(tag4("logwindo"), RVA_NAME_LOGWIN);
        CHECK_EQ(tag4("magic"), RVA_NAME_MAGIC);
        CHECK_EQ(tag4("ability"), RVA_NAME_ABIL);
        CHECK(rva_exam_menu_mine(true, tag4("magic")) && !rva_exam_menu_mine(true, tag4("ability")));
        CHECK(rva_exam_menu_mine(false, tag4("ability")) && !rva_exam_menu_mine(false, tag4("magic")));
        CHECK(!rva_exam_menu_mine(false, 0) && !rva_exam_menu_mine(true, tag4("inline")));
    }

    SECTION("rva menu : a decoy name is not a menu, and only a real name vindicates");
    {   // 41fafcd / ce67b32 : 'inline' counted as a menu being open raised a BLOCK after ninety idle seconds.
        // Mutation : return nm != 0 && !rva_menu_is_decoy(nm); -> return nm != 0;
        CHECK(rva_menu_is_decoy(tag4("inline")) && rva_menu_is_decoy(tag4("logwindo")));
        CHECK(!rva_menu_is_decoy(tag4("magic")) && !rva_menu_is_decoy(0));
        CHECK(!rva_menu_real_name(tag4("inline")));
        CHECK(!rva_menu_real_name(tag4("logwindo")));
        CHECK(!rva_menu_real_name(0));                     // nothing open is not a name either
        CHECK(rva_menu_real_name(tag4("magic")));
        CHECK(rva_menu_real_name(tag4("party")));          // ANY other name : the leader / quartermaster menus too
    }

    SECTION("rva menu : a confirmed slot that read only decoys keeps searching, and is never torn down");
    {   // ce67b32 : a sixty-consecutive-decoys gate kept resetting on 0 and left a wrong cached address unsearched ;
        // un-confirming first made the cost box flicker. Unproven keeps the slot AND the search ; a real name ends it.
        // Mutation : return !(confirmed && realName > 0); -> return !confirmed;
        unsigned real = 0;
        CHECK(rva_menu_keeps_searching(true, real));       // restored from cache, never vindicated : keep looking
        CHECK(rva_menu_keeps_searching(false, real));      // unproven : keep looking
        real = 1;
        CHECK(!rva_menu_keeps_searching(true, real));      // vindicated : final
        CHECK_EQ(1u, real);                                // ...and the verdict survives being asked again
        CHECK(!rva_menu_keeps_searching(true, real));
    }

    SECTION("rva menu : rva break -- an unconfirmed slot inherits no vindication");
    {   // ce67b32 : after //aio rva break the next candidate must earn its own real name.
        // Mutation : if (!confirmed) realName = 0; -> if (false) realName = 0;
        unsigned real = 1;
        CHECK(rva_menu_keeps_searching(false, real));
        CHECK_EQ(0u, real);
        CHECK(rva_menu_keeps_searching(true, real));       // confirmed again later : still has to prove itself
    }

    SECTION("rva menu : the tag buys a working address, never a verdict");
    {   // 0a48920 : every menu-shaped slot carries the tag, decoys included ; confirming on it adopted 'inline'
        // four bytes from 'magic', and every address reported PROVEN while nothing was detected.
        // Mutation : return RVA_PROOF_USABLE; -> return RVA_PROOF_PROVEN;
        CHECK_EQ(RVA_PROOF_USABLE, rva_menu_tag_proof(false, false));
        CHECK(rva_menu_tag_proof(false, false) != RVA_PROOF_PROVEN);
        CHECK_EQ(RVA_PROOF_NONE, rva_menu_tag_proof(false, true));   // it speaks once
    }

    SECTION("rva menu : the tag never pulls a confirmed slot back down");
    {   // ce67b32 : with the sweep running underneath a confirmed slot, re-adopting it unconfirmed would recreate by
        // the back door the gap in which nothing is trusted.
        // Mutation : if (confirmed || adoptedTag) return RVA_PROOF_NONE; -> if (adoptedTag || (confirmed && !confirmed)) return RVA_PROOF_NONE;
        CHECK_EQ(RVA_PROOF_NONE, rva_menu_tag_proof(true, false));
        CHECK_EQ(RVA_PROOF_NONE, rva_menu_tag_proof(true, true));
    }

    SECTION("rva differential : an empty read is not a changed name");
    {   // 725c8d4 : the log-window slot blinks empty for a frame, and the first healer adopted it as "following".
        // Mutation : if (!v) return false; -> if (false) return false;
        const unsigned U = 0xFFFFFFFFu;
        unsigned last = U; bool adopted = false;
        const unsigned logw[8] = { tag4("logwindo"), 0, tag4("logwindo"), 0, 0, tag4("logwindo"), 0, tag4("logwindo") };
        for (int i = 0; i < 8; ++i) if (rva_second_different(last, logw[i], U)) adopted = true;
        CHECK(!adopted);
        unsigned focus = U; int when = -1;
        const unsigned seq[5] = { 0, tag4("magic"), 0, 0, tag4("ability") };
        for (int i = 0; i < 5; ++i) if (when < 0 && rva_second_different(focus, seq[i], U)) when = i;
        CHECK_EQ(4, when);                                 // the second DIFFERENT real name, not the blink
        unsigned z = 0;                                    // the examine scan : 0 is its unset marker
        CHECK(!rva_second_different(z, 0x213, 0));
        CHECK(!rva_second_different(z, 0x213, 0));
        CHECK(rva_second_different(z, 0x214, 0));
    }

    SECTION("rva tie-break : the candidate nearest the anchor wins, not the first found");
    {   // A patch shifts by bytes ; the merit window can hold a 4-byte coincidence.
        // Mutation : return !best || rva_dist(a, anchor) < rva_dist(best, anchor); -> return !best || (rva_dist(a, anchor) < rva_dist(best, anchor) && !best);
        const unsigned anchor = 0x485826;
        const unsigned found[4] = { anchor - 0x3000, anchor + 0x2000, anchor + 6, anchor - 0x10 };
        unsigned best = 0;
        for (int i = 0; i < 4; ++i) if (rva_closer(found[i], anchor, best)) best = found[i];
        CHECK_EQ(anchor + 6, best);
        CHECK(!rva_closer(anchor + 6, anchor, anchor - 6));   // a tie keeps the one already held
    }

    SECTION("rva re-sweep : the candidate set grows, and never drops a watched slot or its memory");
    {   // 725c8d4 : one pass with every menu closed sees only the decoys ; the set must keep growing as menus open.
        // Mutation : if (addr[k] == fresh[i]) { known = true; break; } -> if (false) { known = true; break; }
        unsigned addr[4] = { 0 }, first[4] = { 0 };
        const unsigned U = 0xFFFFFFFFu;
        const unsigned pass1[2] = { 0x621890, 0x621938 };  // menus closed : the two decoys
        int n = rva_merge_candidates(addr, first, 0, 4, pass1, 2, U);
        CHECK_EQ(2, n);
        first[0] = tag4("inline");                         // the differential has started on the first one
        const unsigned pass2[3] = { 0x621938, 0x62188C, 0x621890 };   // Magic open : the focused slot appears
        n = rva_merge_candidates(addr, first, n, 4, pass2, 3, U);
        CHECK_EQ(3, n);
        CHECK_EQ(0x62188Cu, addr[2]);
        CHECK_EQ(U, first[2]);
        CHECK_EQ(tag4("inline"), first[0]);                // memory kept
        const unsigned pass3[3] = { 0x1, 0x2, 0x3 };
        n = rva_merge_candidates(addr, first, n, 4, pass3, 3, U);
        CHECK_EQ(4, n);                                    // capacity holds
    }

    SECTION("rva full sweep : the budget is bounded and closes LOUDLY, exactly once");
    {   // CLAUDE.md rule 10 : a budget that expires must say so, or it reads exactly like nothing happening.
        // Mutation : return !nFresh && sweepsDone == RVA_FULL_SWEEPS; -> return !nFresh && sweepsDone > RVA_FULL_SWEEPS;
        int sweeps = 0, said = 0;
        for (int pass = 0; pass < 20; ++pass) {
            const int nf = 0;                              // nothing menu-shaped anywhere
            if (rva_full_sweep_due(nf, 0, sweeps)) { ++sweeps; if (rva_full_sweep_spent(nf, sweeps)) ++said; }
        }
        CHECK_EQ(RVA_FULL_SWEEPS, sweeps);
        CHECK_EQ(1, said);
        CHECK(!rva_full_sweep_due(0, 2, 0));               // candidates already watched : no stutter
        CHECK(!rva_full_sweep_due(1, 0, 0));               // the cheap window found one
        CHECK(!rva_full_sweep_spent(1, RVA_FULL_SWEEPS));  // the last sweep found one : nothing to confess
    }

    // ---------------------------------------------------------------- the examine caches ----

    SECTION("rva exam decode : spells stop at 0x4000, the ability cache takes JA+0x200 AND raw weapon skills");
    {   // 0cc4f36 : a proposal reading 02D38C98 is no spell. The WS half : a cache is provable from either menu.
        // Mutation : return v >= 1 && t.ws(v); -> return false;
        CHECK(!rva_exam_value_decodes(true, 0x02D38C98u, AnyAction()));
        CHECK(!rva_exam_value_decodes(true, 0x4001, AnyAction()));
        CHECK(!rva_exam_value_decodes(true, 0, AnyAction()));
        CHECK(rva_exam_value_decodes(true, 0x189, FewActions()));
        CHECK(!rva_exam_value_decodes(true, 0x18A, FewActions()));
        CHECK(rva_exam_value_decodes(false, 0x200 + 19, FewActions()));   // 531 -> Manafont
        CHECK(!rva_exam_value_decodes(false, 19, FewActions()));          // raw 19 is not a weapon skill here
        CHECK(rva_exam_value_decodes(false, 3, FewActions()));            // the stale value : it DOES decode
        CHECK(!rva_exam_value_decodes(false, 0, AnyAction()));
        CHECK(!rva_exam_value_decodes(false, 0x4201, AnyAction()));
    }

    SECTION("rva exam cursor : a confirmed cache that decodes but never follows the cursor is refuted");
    {   // 2af148d : `if (confirmed) return;` -- a verdict cached to disk could never be revisited.
        // d0d2f9d : the stale ability cache held 3, which decodes ; only the cursor tells a dead cache from a live one.
        // Mutation : if (++w.dead < moves) return false; -> if (++w.dead < moves * 1000) return false;
        RvaCursorWatch w = { 0, 0, 0 };
        int refutedAt = -1;
        for (unsigned cur = 1; cur <= 8 && refutedAt < 0; ++cur)
            if (rva_exam_cursor_refutes(w, true, cur, 3, 3)) refutedAt = (int)cur;
        CHECK_EQ(4, refutedAt);                            // first sighting seeds the value, then three moves stuck
        CHECK_EQ(0, w.dead);                               // spent : the next refutation starts from scratch
    }

    SECTION("rva exam cursor : a cursor that holds still says nothing about the cache");
    {   // d0d2f9d : the evidence is keyed on the player MOVING -- a cursor parked on one spell is not a stuck cache.
        // Mutation : if (cursor == w.cur) return false; -> if (false) return false;
        RvaCursorWatch w = { 0, 0, 0 };
        bool refuted = false;
        for (int f = 0; f < 600; ++f) if (rva_exam_cursor_refutes(w, true, 5, 0x189, 3)) refuted = true;
        CHECK(!refuted);
    }

    SECTION("rva exam cursor : a cache that follows is alive, and one follow resets the count");
    {   // Three moves with no change is the refutation -- CONSECUTIVE ones, the same evidence the scan adopts on.
        // Mutation : if (value != w.val) { w.val = value; w.dead = 0; return false; } -> if (value != w.val) { w.val = value; return false; }
        RvaCursorWatch w = { 0, 0, 0 };
        const unsigned vals[9] = { 0x21, 0x21, 0x21, 0x22, 0x22, 0x22, 0x23, 0x23, 0x23 };   // lags a move, then follows
        bool refuted = false;
        for (unsigned i = 0; i < 9; ++i) if (rva_exam_cursor_refutes(w, true, i + 1, vals[i], 3)) refuted = true;
        CHECK(!refuted);
    }

    SECTION("rva exam cursor : closing the menu or losing the cursor resets the evidence");
    {   // Two stuck moves, the menu closes, two more after reopening : never three in a row under one open menu.
        // Mutation : if (!menuMine || !cursor) { w.dead = 0; return false; } -> if (!menuMine || !cursor) { return false; }
        RvaCursorWatch w = { 0, 0, 0 };
        bool refuted = false;
        unsigned cur = 1;
        for (int k = 0; k < 3; ++k) if (rva_exam_cursor_refutes(w, true, cur++, 3, 3)) refuted = true;   // seen, then 2 stuck
        for (int round = 0; round < 4; ++round) {
            if (rva_exam_cursor_refutes(w, false, cur, 3, 3)) refuted = true;          // the menu closes...
            for (int k = 0; k < 2; ++k) if (rva_exam_cursor_refutes(w, true, cur++, 3, 3)) refuted = true;   // ...reopens, 2 stuck
            if (rva_exam_cursor_refutes(w, true, 0, 3, 3)) refuted = true;             // no cursor readable
            for (int k = 0; k < 2; ++k) if (rva_exam_cursor_refutes(w, true, cur++, 3, 3)) refuted = true;
        }
        CHECK(!refuted);
    }

    SECTION("rva exam ban : a refuted address cannot be put back by the weak decode proof");
    {   // a65a3d7 : "does not follow the cursor -- reopening the search" then "CONFIRMED [decodes while its own menu is
        // open]", every tick. A refutation has to outrank the evidence it overturned.
        // Mutation : return rightMenu && decodes && rva != banned; -> return rightMenu && decodes && (rva != banned || banned == banned);
        const unsigned stale = 0x6323C8, live = 0x6670B0;
        CHECK(!rva_exam_weak_proof(true, true, stale, stale));
        CHECK(rva_exam_weak_proof(true, true, live, stale));
        CHECK(!rva_exam_weak_proof(false, true, live, 0));
        CHECK(!rva_exam_weak_proof(true, false, live, 0));
        // the argument itself, as the watcher would have seen it : sixty frames of refute / re-confirm
        FlipState fs; flip_reset(fs);
        bool confirmed = true, argued = false; unsigned banned = 0;
        RvaCursorWatch w = { 0, 0, 0 };
        for (unsigned f = 1; f <= 60; ++f) {
            if (confirmed) {
                if (rva_exam_cursor_refutes(w, true, f, 3, 1)) { banned = stale; confirmed = false; }
            } else if (rva_exam_weak_proof(true, true, stale, banned)) confirmed = true;
            if (flip_feed(fs, rva_flip_key(stale, confirmed), 6, 120)) argued = true;
        }
        CHECK(!argued);
        CHECK(!confirmed);
    }

    SECTION("rva exam shift : the sibling names the other cache exactly, and a shift of zero is a proposal");
    {   // 0cc4f36 : the caches sit 0x998 apart and move as one -- 0x6323C8 proven names 0x632D60. And 725c8d4 : an
        // anchor back on its own seed says "this did not move", which stranded the caches when refused.
        // Mutation : return seed + (anchorRva - anchorSeed); -> return seed + (anchorSeed - anchorRva);
        CHECK_EQ(0x632D60u, rva_shift_proposal(0x634F68, 0x6345D0, 0x6323C8));   // 2026-08-12 seeds, -0x2208
        CHECK_EQ(0x667A48u, rva_shift_proposal(0x634F68, 0x6345D0, 0x6670B0));   // the 2026-09-11 region move, +0x32AE0
        CHECK_EQ(0x667A48u, rva_shift_proposal(0x667A48, 0x6670B0, 0x6670B0));   // zero shift : the seed itself
    }

    SECTION("rva exam guess : a guess speaks once, then yields to the scan");
    {   // 5d4f065 : the sibling branch returned unconditionally ; the differential scan below it was never reached.
        // Mutation : if (!said && siblingConfirmed) { -> if (siblingConfirmed) {
        bool said = false;
        CHECK_EQ(RVA_GUESS_SIBLING, rva_exam_guess(said, true, true, false, false));
        CHECK(said);
        CHECK_EQ(RVA_GUESS_NONE, rva_exam_guess(said, true, true, false, false));   // not again : the scan's turn
        bool fresh = false;
        CHECK_EQ(RVA_GUESS_NONE, rva_exam_guess(fresh, false, true, false, true));  // nothing proven : nothing to borrow
        CHECK(!fresh);
    }

    SECTION("rva exam guess : a confirmed sibling is the authority -- the menu-pointer delta never overrides it");
    {   // dd8b70d : the sibling's proposal already matched, the code fell through to the menu pointer, and the other
        // region's delta was proposed over it ("+0x32AE4 [proven shift]" / "-0x2208 [sibling]"). The menu pointer is
        // the fallback for when NEITHER cache is known.
        // Mutation : if (!siblingConfirmed && anchorConfirmed && anchorMoves) return RVA_GUESS_ANCHOR; -> if (anchorConfirmed && anchorMoves) return RVA_GUESS_ANCHOR;
        bool said = false;
        CHECK_EQ(RVA_GUESS_NONE, rva_exam_guess(said, true, false, true, true));    // already on the sibling's answer
        CHECK(said);
        CHECK_EQ(RVA_GUESS_NONE, rva_exam_guess(said, true, false, true, true));    // ...and on every frame after
        said = false;
        CHECK_EQ(RVA_GUESS_SIBLING, rva_exam_guess(said, true, true, true, true));
        CHECK_EQ(RVA_GUESS_NONE, rva_exam_guess(said, true, true, true, true));     // the sibling spoke : the scan, not the menu delta
        bool none = false;
        CHECK_EQ(RVA_GUESS_ANCHOR, rva_exam_guess(none, false, false, true, true)); // the fallback still works
        CHECK_EQ(RVA_GUESS_NONE, rva_exam_guess(none, false, false, true, false));
    }

    // ---------------------------------------------------------------- PointWatch ----

    SECTION("rva PointWatch : an all-zero payload proves nothing");
    {   // fd80423 : the client ZEROES the mirror block until the next 0x061 ; a zero search adopts empty memory.
        // Mutation : return epTnml || xpTnl; -> return epTnml || xpTnl || !xpTnl;
        // Mutation : return maxMerits != 0; -> return maxMerits != 0 || maxMerits == 0;
        CHECK(!rva_pw_payload_proves(0, 0));
        CHECK(rva_pw_payload_proves(56000, 0));            // level capped : no exemplar yet
        CHECK(rva_pw_payload_proves(0, 30000));
        CHECK(!rva_merit_payload_proves(0));
        CHECK(rva_merit_payload_proves(75));
        CHECK(rva_pw_block_agrees(0, 0, 0, 0, 0, 0, 0));   // ...which is exactly why the guard above has to exist
    }

    SECTION("rva PointWatch : the static agrees field by field, and the merit flag bit is not the count");
    {   // 725c8d4 : EXP 55999/56000, LP 9999 against the character sheet.
        // Mutation : ((v0 >> 16) & 0x7F) == merits -> ((v0 >> 16) & 0xFF) == merits
        const unsigned xw = 55999u | (56000u << 16);
        CHECK(rva_pw_block_agrees(xw, 1200, 30000, 55999, 56000, 1200, 30000));
        CHECK(!rva_pw_block_agrees(xw, 1200, 30000, 56000, 55999, 1200, 30000));   // cur and req are not interchangeable
        CHECK(!rva_pw_block_agrees(xw, 1201, 30000, 55999, 56000, 1200, 30000));
        CHECK(!rva_pw_block_agrees(xw, 1200, 30001, 55999, 56000, 1200, 30000));
        CHECK(rva_merit_agrees(9999u | (0x80u << 16) | (12u << 16), 75, 9999, 12, 75));   // flag bit set
        CHECK(!rva_merit_agrees(9999u | (12u << 16), 74, 9999, 12, 75));
        CHECK(!rva_merit_agrees(9998u | (12u << 16), 75, 9999, 12, 75));
    }

    SECTION("rva PointWatch : the merit window beside the 0x061 block reaches the merit block, both measured layouts");
    {   // Mutation : RVA_MERIT_FROM_BLOCK = 0x1E2 -> RVA_MERIT_FROM_BLOCK = 0x5000   (the window no longer reaches)
        const unsigned blocks[2] = { 0x485644, 0x485684 }, merits[2] = { 0x485826, 0x485866 };   // 2026-09-11, 2026-08-12
        for (int i = 0; i < 2; ++i) CHECK(rva_dist(blocks[i] + RVA_MERIT_FROM_BLOCK, merits[i]) < RVA_MERIT_WINDOW);
    }

    // ---------------------------------------------------------------- the watchers ----

    SECTION("rva flipwatch key : a CONFIRMED / un-confirmed cycle on one address is seen");
    {   // ce67b32 : fed the RVA alone, the watcher stayed silent through four cycles -- the address never moved.
        // Mutation : return rva ^ (confirmed ? 0x80000000u : 0u); -> return rva ^ (confirmed ? 0u : 0u);
        FlipState s; flip_reset(s);
        int trips = 0;
        for (int f = 0; f < 20; ++f) if (flip_feed(s, rva_flip_key(0x62188C, (f & 1) != 0), 6, 120)) ++trips;
        CHECK_EQ(1, trips);
        CHECK(rva_flip_key(0x62188C, true) != rva_flip_key(0x62188C, false));
        CHECK(rva_flip_key(0x62188C, false) != rva_flip_key(0x621890, false));
    }

    SECTION("rva watcher : an unproven menu pointer is the only thing reported");
    {   // 3e2bd8b : one root cause, one remedy -- the cache warnings would send the player to the wrong menu.
        // Mutation : return menuConfirmed && !examConfirmed; -> return (menuConfirmed || !menuConfirmed) && !examConfirmed;
        CHECK(!rva_exam_check_due(false, false));
        CHECK(rva_exam_check_due(true, false));
        CHECK(!rva_exam_check_due(true, true));
    }
}
