// song_slots.h -- how many of YOUR songs a person carries, and which one the game drops.
//
// Written 2026-09-10 to replace three rules that shared one broken number. The model behind it is
// docs/songs/04-ce-que-jai-compris.md, every claim of which was measured in game:
//
//   - a song is an INSTANCE : one spell, on one person, with one expiry and a `tenuto` flag ;
//   - the only ordering key is TIME REMAINING -- it names what expires, what the game evicts at the
//     cap, and which of two same-status twins just went ;
//   - the limit is per (SINGER, TARGET). It is not a total the bard holds.
//
// WHAT THIS REPLACES, AND WHY IT COULD NOT BE PATCHED. The old rules compared a count that
//   (a) SKIPPED the "fake" songs -- Gold Capriccio, Goblin Gavotte -- sung for the sole purpose of
//       occupying a slot, because it filtered on song_family() which is 0 for exactly those ; and
//   (b) was GLOBAL, counting distinct spells across every ally at once, a number that exists
//       nowhere in the game.
// Two independent reasons for the same verdict: throw away, do not repair.
#pragma once

namespace aio {

// One of your songs, on one person. `remSec` is exact for your own copy (0x063) and an estimate for
// an ally's -- which is fine here, because this only ever ORDERS them.
struct SlotSong {
    unsigned short spell;
    int            remSec;
    unsigned char  tenuto;   // sung under Tenuto : the game will not overwrite it, by anything
};

// ---- 1. which song the game drops to make room -----------------------------------------------------
//
// MEASURED 2026-09-10: at the cap, the song with the SHORTEST REMAINING duration is the one that goes.
// BG Wiki says the same; a Square Enix forum thread says "the oldest" instead, and the two rules agree
// in almost every observable case -- a dummy song is sung with no duration gear, so it is at once the
// oldest and the shortest, and both rules point at it. That is why one can be coded while the other is
// believed. The test that separates them (an old-but-long song beside a fresh-but-short one) was run:
// the short one went.
//
// Tenuto is the exception, and it is categorical rather than a modifier: a Tenuto'd song "will not
// subsequently be overwritten by other songs". Verified in game -- singing at the cap under Tenuto
// still evicts something, so Tenuto does NOT raise the limit; it only removes its own song from the
// pool of victims.
//
// Returns the index of the song that would go, or -1 when every song is protected -- a state we have
// never produced in game, and which the caller must therefore treat as "unknown", not as "nothing".
inline int song_eviction_victim(const SlotSong* songs, int n) {
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if (songs[i].tenuto) continue;
        if (best < 0 || songs[i].remSec < songs[best].remSec) best = i;
    }
    return best;
}

// ---- 2. was this loss the game making room, or something taken from you? ----------------------------
//
// A song pushed out to fit a new one is a choice you made, and must not raise a red alert. A song
// dispelled is exactly what the alert exists for. Both look identical from the buff list alone -- the
// icon is simply gone -- so the two are told apart by WHAT went, not by when:
//
//   the song that vanished is the one the eviction rule NAMES, and another of your songs landed on
//   that same person a moment ago.
//
// No cap enters into this, and that is the point. The game only evicts when the set is full, so a
// vanishing that coincides with a landing AND lands on the predicted victim is an eviction; the same
// vanishing on any OTHER song was taken from you, and still alerts. The old rule asked "am I at the
// cap?" of a count that could not answer it.
inline bool song_made_room(int victimIdx, int lostIdx, unsigned sinceLandedMs, unsigned windowMs) {
    if (victimIdx < 0 || lostIdx < 0 || victimIdx != lostIdx) return false;
    return sinceLandedMs <= windowMs;
}

// ---- 3. the cap, learned from the game admitting it -------------------------------------------------
//
// The number of songs you can hold depends on the INSTRUMENT equipped, so it is not a constant, and a
// high-water mark of observed counts is the wrong way to find it: it learns the maximum reached under
// a Daurdabla and keeps it after the swap. The old code did exactly that, and a plugin reload or a job
// change reset it to 1, from where it silenced every song loss until a full rotation rebuilt it.
//
// An EVICTION is the game stating the cap outright: it makes room only when there is none left, so the
// count at that moment IS the limit. One event, exact, and it re-teaches itself the moment the
// instrument changes -- no window, no high-water mark, nothing to poison.
struct SlotCap { int cap; bool valid; };

inline SlotCap song_cap_learn(SlotCap cur, int countNow, bool evicted) {
    if (evicted && countNow > 0) { cur.cap = countNow; cur.valid = true; }
    return cur;
}

// ---- 4. a lost song that cannot be brought back -----------------------------------------------------
//
// Clarion Call opens a fifth slot. The song placed in it OUTLIVES the ability -- it can be re-sung over
// itself for as long as you like -- so losing the ability is not losing the slot. But once that song is
// actually GONE and Clarion Call is not available, there is no way back to five, and a permanent red
// alert for it is noise: "pour 4 songs c'est ok".
//
// Every condition narrows a way of being wrong:
//   - the cap must be KNOWN, and known from an eviction rather than guessed ;
//   - the song must be on an ALLY, not on you ;
//   - Clarion Call must be unavailable -- with it up or ready the slot is refillable, and a refillable
//     song keeps its alert, which is the whole point of having one ;
//   - the count must land EXACTLY on the cap: you held cap + 1, you lost one, you are at cap. "At or
//     above" reads true for any count once the cap is understated, which is how an unlearned cap used
//     to silence real losses.
inline bool song_unrecoverable(const SlotCap& c, bool onSelf, bool isSong,
                               bool ccUsable, int countNow) {
    return c.valid && !onSelf && isSong && !ccUsable && countNow == c.cap;
}

} // namespace aio
