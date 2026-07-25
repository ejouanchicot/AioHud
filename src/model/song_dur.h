// song_dur.h -- BRD song duration for a song cast on an ally, reproducing the Windower Timers model
// (RE'd from Timers.dll FUN_10007a40 ; the decode lives in scratchpad/timers_song.txt).
//
//   dur = Base (120 ; Miracle Cheer -> 900) x m1 x m2 x m3 + a3
//   m1 = 1 + SUM(per-item FLAT "song duration +%") + SUM(per-item extra % for THIS song's FAMILY) + 0.05 JP
//   m2 = 1 + 1.0 if Troubadour (status 348)                                   -> x2
//   m3 = 1 + 0.5 if Soul Voice(52) or Marcato(231) AND song family in {11 Hymnus, 12 Mazurka, 14 Scherzo}
//   a3 = flat merit seconds : Marcato(231) -> merit@+0x148. Clarion Call / Tenuto do NOT touch duration.
//
// The item table is GENERATED (song_dur_gen.h, scripts/gen_song_dur.py) straight from the RE dump. It used to
// be a hand transcription, and the hand step lost two things that together underestimated every ally song row
// by 22-37% when measured against the server (//aio songlog, 2026-07-25) :
//   - every item that ALSO carries a family extra had its FLAT column zeroed (Fili Calot +3 is +10 flat AND
//     +10% Madrigal ; it was stored as flat 0), and two Carnwenhan ilvl stages were dropped outright (-50%) ;
//   - the family term was then discarded here as well, on the belief that those columns were "+1 <Song>"
//     POTENCY mis-read by the RE. They are not. Proof: Madrigal and Minuet cast on the SAME sixteen equipped
//     ids, no Troubadour, same 120 s base, ran 352 s vs 340 s -- exactly the +10% of one extra family piece.
//     The RE's own reconciliation says it plainly: m1 = 1 + Sum(flat) + Sum(family-extra for THIS song) + JP.
//
// Even correct, this is a MODEL over a hardcoded table that cannot know an item the RE never saw (Gjallarhorn,
// Marsyas and Daurdabla are absent from Timers' table too, so Timers is a few percent short on those sets).
// It is therefore the FALLBACK only: PartyState learns the real factor off the server's own 0x063 whenever a
// song lands on you, and prefers that. See PartyState::songdur_check.
#pragma once
#include "model/song_family_gen.h"   // song_family(spell) : spell id -> song family id
#include "model/song_dur_gen.h"      // SONG_DUR[] : per-item flat % + per-family extra % (generated)
namespace aio {

static const unsigned short MIRACLE_CHEER_ID = 0x56E9;   // RANGE slot -> flat 900 s song

// m1 % for a given song = the FLAT gear (applies to every song) + the extra carried by the pieces that
// happen to boost THIS song's family. `songFamily` 0 (a song with no family, e.g. Goblin Gavotte) simply
// collects no extra -- which is exactly why the dummy songs measured at flat-only rates.
inline int song_dur_m1_pct(const unsigned short ids[16], int songFamily) {
    int p = 0;
    for (int s = 0; s < 16; ++s) {
        if (!ids[s]) continue;
        for (int i = 0; i < SONG_DUR_N; ++i) {
            if (SONG_DUR[i].id != ids[s]) continue;
            p += SONG_DUR[i].flat;
            if (songFamily > 0)
                for (int f = 0; f < (int)(sizeof(SONG_DUR[i].fam) / sizeof(SONG_DUR[i].fam[0])); ++f)
                    if (SONG_DUR[i].fam[f].family == (unsigned char)songFamily) p += SONG_DUR[i].fam[f].pct;
            break;   // an item occupies one slot : stop at the first match
        }
    }
    return p;
}
inline bool has_miracle_cheer(const unsigned short ids[16]) { return ids[2] == MIRACLE_CHEER_ID; }   // slot 2 = range

} // namespace aio
