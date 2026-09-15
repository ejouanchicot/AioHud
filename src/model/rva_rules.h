// rva_rules.h -- the DECISIONS of the FFXiMain static-address healers (model/ffximain_rva.cpp), each as a function
// of its inputs.
//
// Cut out on 2026-09-14. ffximain_rva.cpp had fifteen fix commits and not one offline test : every branch below is
// a defect that reached a player after a client patch, and the comment beside it says which. The .cpp keeps what
// cannot be tested without the game -- the SEH-guarded reads, the image walk, the logs -- and asks these rules what
// to conclude from what it read. tests/t_rvarules.cpp pins each one, and names the mutation that proves it bites.
//
// Pure on purpose : no windows.h, no game memory, no Windower. A value the rule needs is a parameter ; a lookup it
// needs (does this id name a real spell ?) is a SOURCE, any type with the named member functions.
#pragma once
#include "model/ffximain_rva.h"   // FM_N
#include <cstdio>

namespace aio {

// ---- the names the healers compare, as the u32 they are read as (first four chars, little-endian) ------------
const unsigned RVA_TAG_MENU    = 0x756E656Du;   // "menu" : def+0x46 of every menu-shaped object, decoys included
const unsigned RVA_NAME_INLINE = 0x696C6E69u;   // "inli" : the 'inline' decoy -- also the REAL slot while the chat input has focus
const unsigned RVA_NAME_LOGWIN = 0x77676F6Cu;   // "logw" : the 'logwindo' decoy
const unsigned RVA_NAME_MAGIC  = 0x6967616Du;   // "magi"
const unsigned RVA_NAME_ABIL   = 0x6C696261u;   // "abil"

// ---- 1. the disk cache : when a file is trusted -----------------------------------------------------------------
//
// Healing happens once per client version, not once per login. The fingerprint is the whole safety of this : a
// cache written for another client must be IGNORED, never trusted, or a stale address gets read as live data --
// the one failure mode worse than a dead feature.
//
// `format` is bumped whenever a HEALER changes its mind about what counts as proof. Without it, a wrong address
// adopted by an older, weaker rule comes back from disk marked "confirmed" and outlives the fix -- which is exactly
// what happened when the first menu healer adopted the log-window slot. A file with no format= line is
// pre-versioned : its addresses are ignored.
enum RvaCacheStatus { RVA_CACHE_OK = 0, RVA_CACHE_BAD_FORMAT, RVA_CACHE_BAD_CLIENT };
struct RvaCacheResult {
    int      status;        // RvaCacheStatus -- a BAD_* stops the parse at that line ; the seeds stay
    int      fmt;           // the format= the file declared (for the log)
    unsigned fp;            // the fingerprint= it declared
    int      n;             // entries accepted
    unsigned rva[FM_N];
    bool     got[FM_N];
};

inline void rva_cache_parse(const char* text, int formatWanted, unsigned fpNow, RvaCacheResult& r) {
    r.status = RVA_CACHE_OK; r.fmt = 0; r.fp = 0; r.n = 0;
    for (int i = 0; i < FM_N; ++i) { r.rva[i] = 0; r.got[i] = false; }
    if (!text) return;
    bool fpOk = false, fmtOk = false;
    while (*text) {
        char line[256]; int k = 0;
        while (*text && *text != '\n' && k < (int)sizeof(line) - 1) line[k++] = *text++;
        if (*text == '\n') ++text;
        line[k] = 0;
        if (line[0] == '#') continue;
        unsigned v = 0; int idx = 0, fmt = 0;
        if (sscanf(line, "format=%d", &fmt) == 1) {
            r.fmt = fmt; fmtOk = (fmt == formatWanted);
            if (!fmtOk) { r.status = RVA_CACHE_BAD_FORMAT; return; }
            continue;
        }
        if (sscanf(line, "fingerprint=%X", &v) == 1) {
            r.fp = v; fpOk = (v == fpNow);
            if (!fpOk) { r.status = RVA_CACHE_BAD_CLIENT; return; }   // the game was patched : seeds stay, healing re-derives
            continue;
        }
        if (!fpOk || !fmtOk) continue;                   // a file with no format= line is pre-versioned : ignore its addresses
        if (sscanf(line, "rva%d=%X", &idx, &v) == 2 && idx >= 0 && idx < FM_N && v) {
            r.rva[idx] = v; r.got[idx] = true; ++r.n;
        }
    }
}

// Only PROVEN addresses are written. A proposal (an address borrowed from another static's shift) is a working
// hypothesis : persisting it would promote it to fact on the next login, unexamined.
// Source : rva(i) confirmed(i) name(i) how(i). Returns the length written (truncated to cap, always terminated).
template <class Src>
inline int rva_cache_write(char* buf, int cap, int format, unsigned fp, const Src& s) {
    if (!buf || cap <= 0) return 0;
    int n = snprintf(buf, cap, "# AioHUD -- FFXiMain static addresses re-derived at runtime.\n"
                               "# Tied to one client build : a different fingerprint discards the whole file.\n"
                               "format=%d\nfingerprint=%08X\n", format, fp);
    for (int i = 0; i < FM_N && n >= 0 && n < cap; ++i)
        if (s.confirmed(i)) n += snprintf(buf + n, cap - n, "rva%d=%X  # %s (%s)\n", i, s.rva(i), s.name(i), s.how(i));
    buf[cap - 1] = 0;
    return (n < 0) ? 0 : (n < cap ? n : cap - 1);
}

// ---- 2. an adoption : what is new about it ----------------------------------------------------------------------
//
// A no-op adoption (same rva) only upgrades the confirmed flag. Anything else is "nothing new to say" and must stay
// silent : several callers re-adopt every frame they see their proof, and each non-noop adoption logs a line and
// rewrites the cache file.
enum RvaAdopt { RVA_ADOPT_NOOP = 0, RVA_ADOPT_MOVE, RVA_ADOPT_CONFIRM };
inline int rva_adopt_verdict(unsigned current, unsigned rva, bool healed, bool confirmedNow, bool confirmedArg) {
    const bool moved = (current != rva);
    if (!moved && healed && (confirmedNow || !confirmedArg)) return RVA_ADOPT_NOOP;
    return moved ? RVA_ADOPT_MOVE : RVA_ADOPT_CONFIRM;
}

// ---- 3. the menu pointer : decoys, and what vindicates a slot -----------------------------------------------------
//
// 'inline' and 'logwindo' are always themselves. But the CORRECT slot also reads 'inline' for as long as the chat
// input holds the focus -- so seeing a decoy name proves nothing whatsoever about a slot ; only never seeing
// anything ELSE does. Twice this false equivalence shipped : a runtime rule that un-confirmed on a second of
// 'inline' (flickering the cost box), and a watcher check that raised a BLOCK after ninety idle seconds.
inline bool rva_menu_is_decoy(unsigned nm) { return nm == RVA_NAME_INLINE || nm == RVA_NAME_LOGWIN; }

// A NAME HAS TO BE SHAPED LIKE A NAME. The differential below adopts the slot that shows two DIFFERENT non-empty
// values, and nothing ever said those values had to look like menu names. MEASURED 2026-09-15, from the line the
// previous incident added for exactly this purpose :
//     fm: menu candidate FFXiMain+0x621C14 read 'wind' then '.8..' ; the slot in service (+0x62188C) reads '....'
// Four bytes of binary that merely changed took the pointer away from a slot that reads 'partywin'. The party
// picker recognises its menu BY NAME, so the Quartermaster / Distribution cursor went dead -- and the wrong
// address was written to the cache as PROVEN, so it came back at every load : a redeploy looked like the cause.
// The game's def names are inline lowercase ASCII tags -- 'partywin', 'inline  ', 'logwindo', 'magic', 'ability'.
// So: four bytes, the first a lowercase letter, the rest lowercase / digit / space / '_'. It costs nothing to be
// strict here : it takes two DIFFERENT names to adopt, and the focused slot shows a dozen over a session.
inline bool rva_menu_name_shaped(unsigned nm) {
    for (int i = 0; i < 4; ++i) {
        const unsigned c = (nm >> (8 * i)) & 0xFFu;
        const bool low = c >= 'a' && c <= 'z';
        if (i == 0) { if (!low) return false; continue; }
        if (!(low || (c >= '0' && c <= '9') || c == ' ' || c == '_')) return false;
    }
    return true;
}

// A REAL MENU NAME VINDICATES THE SLOT FOR GOOD : a slot that ever shows anything else IS the focused menu.
// Junk vindicates nothing -- it is not a name, it is a slot that happens to hold changing bytes.
inline bool rva_menu_real_name(unsigned nm) { return nm != 0 && rva_menu_name_shaped(nm) && !rva_menu_is_decoy(nm); }

// A CONFIRMATION THAT READS ONLY DECOYS IS REVISABLE -- BUT IT IS NEVER TORN DOWN FIRST. A slot vindicated by a real
// name is final. A confirmed one that is not is merely UNPROVEN : it stays in use while the two-different-names
// sweep keeps running underneath it. Being unproven is enough to keep looking -- an earlier gate on sixty
// CONSECUTIVE decoy frames was reset by every 0 (nothing open), and left a wrong cached address unsearched.
// An unconfirmed slot carries no vindication : //aio rva break must not let the next candidate inherit one.
inline bool rva_menu_keeps_searching(bool confirmed, unsigned& realName) {
    if (!confirmed) realName = 0;
    return !(confirmed && realName > 0);
}

// What the "menu" tag buys. THE TAG PROVES "A MENU", NOT "THE FOCUSED MENU" : every menu-shaped slot carries it,
// the decoys included, and this test used to CONFIRM on it -- the seed landed on 'inline' four bytes from 'magic'
// and every address reported PROVEN while nothing was detected. So it buys a working address, never a verdict.
// ...and it must never PULL A CONFIRMED SLOT BACK DOWN : re-adopting unconfirmed would recreate by the back door
// the gap the sweep-underneath shape removed. It speaks once, then leaves the floor to real evidence.
enum RvaProof { RVA_PROOF_NONE = 0, RVA_PROOF_USABLE, RVA_PROOF_PROVEN };
inline int rva_menu_tag_proof(bool confirmed, bool adoptedTag) {
    if (confirmed || adoptedTag) return RVA_PROOF_NONE;
    return RVA_PROOF_USABLE;
}

// ---- 4. the differential : a value that CHANGES -----------------------------------------------------------------
//
// EMPTY IS NOT A NAME. Treating "no menu right now" as a changed name is what made the first menu healer adopt the
// log window : that slot blinks empty for a frame and instantly looked like it was following the player. Only
// NON-EMPTY values count, and it takes TWO DIFFERENT ones. `first` is the per-candidate memory, `unset` its empty
// marker. The examine scan runs the same differential on decoded ids.
inline bool rva_second_different(unsigned& first, unsigned v, unsigned unset) {
    if (!v) return false;
    if (first == unset) { first = v; return false; }
    return first != v;
}

// Tie-break between several candidates that proved themselves : a patch shifts by BYTES, so the one nearest the
// anchor wins. `best` = 0 means none yet. With only ~4 bytes of signal (the merit block) a window can hold a
// coincidence, and distance is the tie-breaker the signature itself cannot provide.
inline unsigned rva_dist(unsigned a, unsigned b) { return (a > b) ? (a - b) : (b - a); }
inline bool rva_closer(unsigned a, unsigned anchor, unsigned best) {
    return !best || rva_dist(a, anchor) < rva_dist(best, anchor);
}

// RE-SWEEP, not sweep-once. A slot only looks like a menu WHILE a menu is open, so a pass taken with everything
// closed sees only the always-present decoys. The candidate set GROWS : a union that never drops a slot already
// watched (nor its differential memory). Returns the new count.
inline int rva_merge_candidates(unsigned* addr, unsigned* first, int n, int cap, const unsigned* fresh, int nf, unsigned unset) {
    for (int i = 0; i < nf && n < cap; ++i) {
        bool known = false;
        for (int k = 0; k < n; ++k) if (addr[k] == fresh[i]) { known = true; break; }
        if (!known) { addr[n] = fresh[i]; first[n] = unset; ++n; }
    }
    return n;
}

// The 12 MB whole-image sweep is the rare fallback, and a BOUNDED one : it would stutter the game for as long as no
// menu had ever been opened. When the budget is spent with nothing found it must SAY SO, exactly once (rule 10).
const int RVA_FULL_SWEEPS = 2;
inline bool rva_full_sweep_due(int nFresh, int nCand, int sweepsDone) {
    return !nFresh && !nCand && sweepsDone < RVA_FULL_SWEEPS;
}
inline bool rva_full_sweep_spent(int nFresh, int sweepsDone) { return !nFresh && sweepsDone == RVA_FULL_SWEEPS; }

// ---- 5. the examine caches --------------------------------------------------------------------------------------
inline bool rva_exam_menu_mine(bool spellCache, unsigned tag) {
    return spellCache ? (tag == RVA_NAME_MAGIC) : (tag == RVA_NAME_ABIL);
}

// Does this raw value decode to a real action for that cache ? The ONLY test either cache ever gets to pass.
// No spell id is above 0x4000 (a proposal reading 02D38C98 was not a spell). The ability cache serves BOTH lists :
// job abilities carry id+0x200, weapon skills the raw id -- accepting only the job-ability half would leave the
// cache unprovable for anyone who opens the WS menu and never the JA one. (And therefore a stale 3 DECODES, as a
// weapon skill : decoding can never be what refutes an address -- see rva_exam_cursor_refutes.)
// Source : spell(id) abil(id) ws(id), each true when the table names that id.
template <class Src>
inline bool rva_exam_value_decodes(bool spellCache, unsigned v, const Src& t) {
    if (spellCache) return v != 0 && v <= 0x4000 && t.spell(v);
    if (v >= 0x200) return v <= 0x4200 && t.abil(v - 0x200);
    return v >= 1 && t.ws(v);
}

// DECODING IS NOT THE TEST -- FOLLOWING THE CURSOR IS. A confirmed cache used to be final (`if (confirmed) return;`),
// so a wrong verdict restored from disk could never be revisited ; then its first refutation asked "does it decode
// ?", and the stale ability cache held 3, a plausible weapon-skill id -- it decoded, it simply never changed. The
// menu's own highlight index says when the player MOVED : a live cache changes with it, a dead one sits still.
// `moves` moves with no change is the refutation -- the same evidence the scan ADOPTS on. Call only while the
// cache's own menu is open ; any frame where it is not (or no cursor reads) resets the count.
struct RvaCursorWatch { unsigned cur, val; int dead; };
inline bool rva_exam_cursor_refutes(RvaCursorWatch& w, bool menuMine, unsigned cursor, unsigned value, int moves) {
    if (!menuMine || !cursor) { w.dead = 0; return false; }
    if (cursor == w.cur) return false;                              // the player has not moved : nothing is being said
    w.cur = cursor;
    if (value != w.val) { w.val = value; w.dead = 0; return false; }   // it followed : alive
    if (++w.dead < moves) return false;
    w.dead = 0;
    return true;
}

// "Decodes while its own menu is open" is the WEAK proof, enough only while nothing contradicts it. An address
// already refuted for standing still under a moving cursor is contradicted : re-confirming it undid the refutation
// on the very next tick, and the two rules traded the slot at 60 Hz. A refutation has to outrank the weaker
// evidence it overturned, so a refuted RVA is BANNED for that cache.
inline bool rva_exam_weak_proof(bool rightMenu, bool decodes, unsigned rva, unsigned banned) {
    return rightMenu && decodes && rva != banned;
}

// A shift PROVEN by another static is a legitimate proposal. A shift of ZERO is a legitimate answer too -- "this
// static did not move" -- and refusing it stranded the caches whenever the anchor came back to its own seed.
inline unsigned rva_shift_proposal(unsigned seed, unsigned anchorSeed, unsigned anchorRva) {
    return seed + (anchorRva - anchorSeed);
}

// WHICH GUESS, IF ANY. A recompile moves REGIONS, not the image : the menu pointer moved +0x32AE4 while both
// caches moved -0x2208, so anchoring a cache on the menu pointer proposed garbage. The caches sit 0x998 apart and
// travel together, so a CONFIRMED SIBLING IS THE AUTHORITY -- not merely the first to speak : a first cut fell
// through to the menu-pointer branch whenever the sibling's proposal already matched, re-proposing the other
// region's delta every frame. And a guess is worth ONE pass : a sibling branch that returned unconditionally kept
// the differential scan (below it) unreachable for ever. The menu pointer is the fallback for when neither cache
// is known. `said` is the per-cache "the sibling has spoken" latch.
//
// ...and "the fallback" means ONLY then. The once-then-yield latch reopened the very fall-through above : with the
// sibling confirmed, a pass whose sibling proposal already matched (or any pass after it spoke) went on to the
// menu-pointer branch, which replaced the sibling's exact answer with the other region's delta -- with no menu
// open to confirm it first, and the scan only reaching +/-32 KB of the SEED. So a confirmed sibling silences the
// anchor for good ; if its answer is wrong, the scan is what finds the right one.
enum RvaGuess { RVA_GUESS_NONE = 0, RVA_GUESS_SIBLING, RVA_GUESS_ANCHOR };
inline int rva_exam_guess(bool& said, bool siblingConfirmed, bool siblingMoves, bool anchorConfirmed, bool anchorMoves) {
    if (!said && siblingConfirmed) {
        said = true;
        if (siblingMoves) return RVA_GUESS_SIBLING;
    }
    if (!siblingConfirmed && anchorConfirmed && anchorMoves) return RVA_GUESS_ANCHOR;
    return RVA_GUESS_NONE;
}

// ---- 6. the PointWatch blocks : packet ground truth ---------------------------------------------------------------
//
// An all-zero payload proves nothing -- and it would match: the client ZEROES the 0x061 mirror block until the next
// 0x061 arrives, so a zero search adopts the first empty stretch of the image.
inline bool rva_pw_payload_proves(unsigned xpTnl, unsigned epTnml) { return epTnml || xpTnl; }
inline bool rva_merit_payload_proves(unsigned maxMerits) { return maxMerits != 0; }

// Does the static still hold what the packet sent ? `xw` = the u32 at +0 (EXP cur | req << 16), `ec`/`et` the
// exemplar pair at +0x58/+0x5C. The merit count's high bit is a flag, not part of the count.
inline bool rva_pw_block_agrees(unsigned xw, unsigned ec, unsigned et, unsigned xpCur, unsigned xpTnl, unsigned epCur, unsigned epTnml) {
    return (xw & 0xFFFF) == xpCur && ((xw >> 16) & 0xFFFF) == xpTnl && ec == epCur && et == epTnml;
}
inline bool rva_merit_agrees(unsigned v0, unsigned v4, unsigned lp, unsigned merits, unsigned maxMerits) {
    return (v0 & 0xFFFF) == lp && ((v0 >> 16) & 0x7F) == merits && (v4 & 0xFF) == maxMerits;
}
// The merit block is searched in a WINDOW beside the 0x061 block (the disassembly puts them side by side ; the
// signature is too short for an image-wide search).
const unsigned RVA_MERIT_FROM_BLOCK = 0x1E2, RVA_MERIT_WINDOW = 0x4000;

// ---- 7. what is watched, and what is reported ---------------------------------------------------------------------
//
// THE VERDICT, NOT JUST THE ADDRESS. The oscillation watcher was fed the RVA alone and stayed silent through four
// CONFIRMED / un-confirmed cycles -- the address never moved ; whether we BELIEVED it did.
inline unsigned rva_flip_key(unsigned rva, bool confirmed) { return rva ^ (confirmed ? 0x80000000u : 0u); }

// ONE ROOT CAUSE, ONE MESSAGE. An unproven menu pointer is why neither examine cache can confirm, so it is the only
// thing reported : the two cache warnings would name the wrong remedy.
inline bool rva_exam_check_due(bool menuConfirmed, bool examConfirmed) { return menuConfirmed && !examConfirmed; }

} // namespace aio
