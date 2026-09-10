// t_songslots.cpp -- the per-person song slot accounting (model/song_slots.h).
//
// Every case here comes from a measurement made in game on 2026-09-10, and the three cases marked
// with a symptom are the defects this file exists to prevent coming back.
#include "check.h"
#include "model/song_slots.h"

using namespace aio;

void test_song_slots() {

    SECTION("the game drops the SHORTEST remaining, not the oldest");
    {   // The two rules agree in almost every observable case, which is exactly why one can be coded
        // while the other is believed: a dummy song is sung with no duration gear, so it is at once the
        // oldest and the shortest. The case that separates them was run in game -- an old-but-long song
        // beside a fresh-but-short one -- and the short one went.
        const SlotSong s[] = { { 417, 300, 0 },    // sung first, but under Troubadour -> long
                               { 397,  40, 0 },    // sung last, bare -> short
                               { 400, 200, 0 } };
        CHECK_EQ(1, song_eviction_victim(s, 3));
    }
    {   // Tenuto is categorical, not a modifier: a Tenuto'd song is simply not a candidate.
        const SlotSong s[] = { { 417, 300, 0 }, { 397, 40, 1 }, { 400, 200, 0 } };
        CHECK_EQ(2, song_eviction_victim(s, 3));   // the shortest is protected -> the next shortest goes
    }
    {   // ...and it does NOT raise the cap. Verified in game: singing at the cap under Tenuto still
        // evicts something. So a set of only-Tenuto songs has no victim, and that is "unknown", never
        // "nothing happened" -- the caller must not read -1 as permission to stay silent.
        const SlotSong s[] = { { 417, 300, 1 }, { 397, 40, 1 } };
        CHECK_EQ(-1, song_eviction_victim(s, 2));
        CHECK_EQ(-1, song_eviction_victim(s, 0));   // nothing held at all
    }

    SECTION("making room is not the same as being dispelled");
    {   // Reported 2026-09-10: "j'ai remplace Minuet V par Victory March et Minuet V est en alerte".
        // At the cap, Victory March was sung and the game dropped Minuet V -- a choice, not a loss.
        // The tell is WHAT went: the song the eviction rule names, while another song was landing.
        CHECK(song_made_room(/*victim*/1, /*lost*/1, /*sinceLanded*/200u, /*window*/6000u));
    }
    {   // A different song went while a cast was landing -> that one was taken from you. It still
        // alerts, and this is the case the old rule swallowed: it asked "am I at the cap?" of a count
        // that could not answer, so ANY loss near a cast was silenced.
        CHECK(!song_made_room(/*victim*/1, /*lost*/2, 200u, 6000u));
    }
    {   // Nothing landed -> nothing made room, whatever went.
        CHECK(!song_made_room(1, 1, /*sinceLanded*/9000u, 6000u));
    }
    {   // No victim could be named (every song protected) -> we do not know, so we do not silence.
        CHECK(!song_made_room(/*victim*/-1, /*lost*/1, 200u, 6000u));
    }

    SECTION("the cap is learned from an eviction, never from a high-water mark");
    {   // The cap follows the INSTRUMENT, so a maximum-ever-seen is structurally wrong: it learns the
        // count reached under a Daurdabla and keeps it after the swap. An eviction is the game saying
        // the set was full -- one event, exact.
        SlotCap c; c.cap = 0; c.valid = false;
        c = song_cap_learn(c, /*countNow*/5, /*evicted*/false);
        CHECK(!c.valid);                                   // a full set proves nothing on its own
        c = song_cap_learn(c, /*countNow*/4, /*evicted*/true);
        CHECK(c.valid);
        CHECK_EQ(4, c.cap);
    }
    {   // And it must be able to go DOWN -- that is the whole reason for not using a high-water mark.
        // Swap to an instrument with fewer slots and the next eviction re-teaches the smaller cap.
        SlotCap c; c.cap = 5; c.valid = true;
        c = song_cap_learn(c, /*countNow*/3, /*evicted*/true);
        CHECK_EQ(3, c.cap);
    }
    {   // An eviction with nothing held is not a fact about anything.
        SlotCap c; c.cap = 4; c.valid = true;
        c = song_cap_learn(c, 0, true);
        CHECK_EQ(4, c.cap);
    }

    SECTION("the fifth Clarion Call song goes quietly, and nothing else does");
    {   // "pour 4 songs c'est ok" : you held cap + 1, the extra went, Clarion Call is not available,
        // so five is out of reach -- a permanent red alert for it is noise.
        SlotCap c; c.cap = 4; c.valid = true;
        CHECK(song_unrecoverable(c, /*onSelf*/false, /*isSong*/true, /*ccUsable*/false, /*countNow*/4));
    }
    {   // Every condition is load-bearing.
        SlotCap c; c.cap = 4; c.valid = true;
        CHECK(!song_unrecoverable(c, false, true,  /*ccUsable*/true,  4));   // refillable -> alert
        CHECK(!song_unrecoverable(c, true,  true,  false, 4));               // on yourself -> your problem
        CHECK(!song_unrecoverable(c, false, false, false, 4));               // not a song
        SlotCap unlearned; unlearned.cap = 0; unlearned.valid = false;
        CHECK(!song_unrecoverable(unlearned, false, true, false, 4));        // an unknown cap silences nothing
    }
    {   // THE COUNT MUST LAND EXACTLY ON THE CAP. Measured 2026-09-10: a plugin reload wiped the old
        // learned cap and it sat at 1 while four songs were up; read as "at or above", losing any one
        // of the four satisfied "3 >= 1" and was silenced. Landing exactly on the cap is what "only the
        // extra song went" actually means.
        SlotCap low; low.cap = 1; low.valid = true;
        CHECK(!song_unrecoverable(low, false, true, false, /*countNow*/3));   // four songs, cap one -> alert
        SlotCap c; c.cap = 4; c.valid = true;
        CHECK(!song_unrecoverable(c, false, true, false, /*countNow*/3));     // one short -> a real loss
        CHECK(!song_unrecoverable(c, false, true, false, /*countNow*/5));     // nothing went at all
    }
}
