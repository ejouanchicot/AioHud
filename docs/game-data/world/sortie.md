---
title: Sortie tracker — gallimaufry, bosses, shards, coffers and items (zone tracker mode 7)
summary: Everything the Sortie box shows comes from four 0x02A zone messages whose ids sit at fixed offsets from the gallimaufry payout, which proves its own id by arithmetic ; measured on two full runs recorded on both clients (2026-09-14).
source: model/party_state_zonetracker.cpp (zt_set_zone mode 7, on_2a SORTIE block, soHeal_), model/party_state.h (ZoneTracker so* fields), ui/hud_zonetracker.cpp (the box), tests/t_packets.cpp + tests/fixtures_sortie_run.h
---
# Sortie tracker (zone tracker mode 7)

**Zone.** Outer Ra'Kaznar [U2] = zone **133**, the only one measured. The basement is the same zone (no zone change).
Leaving to **Kamihr Drifts (267)** freezes the run as "last run" (the box stays, the counters stop) ; entering 133
again starts a clean run. Any other exit clears the mode. A reload inside the run restores it from the zone cache.

## The evidence

Two complete runs recorded with `//aio pcap start` on both clients, 2026-09-14 21:20-22:24 (tapes
`sortie_tetsouo_20260914_212028` 154 057 packets, `sortie_kaories_20260914_212059` 126 822). Every figure below was
checked against a second, independent source in the same tape : the inventory stack counts (0x020 / 0x01F, bag 0),
the temporary bag (bag 3) and the key-item tables (0x055).

## The four messages (0x02A, id masked 0x7FFF, params p1..p4 @0x08..0x14)

| id (2026-09-14) | offset | params | meaning |
|---|---|---|---|
| **7238** | G | `[gain, new banked total]` | gallimaufry payout |
| **7236** | G - 2 | `[item]` 9906..9913 | Ra'Kaznar Shard #A..#H obtained |
| **7237** | G - 1 | `[item]` 9906..9913 | that shard handed over = its boss defeated |
| **6395** | G - 843 | `[item, gallimaufry total, ?, ?]` | an item obtained (Old Case 6614, Ra'Kaz. Sapphire 9927, Hexahedrite 9931) |

**Every one of them arrives twice**, in the same second, byte-identical.

- **Gallimaufry** is counted from the TOTAL : `run = total - baseline`, baseline = the first payout's `total - gain`.
  Never by summing gains : one +45 of the recorded run had no message of its own, and the next total covered it. A
  duplicate carries the same total, so it counts nothing. The run of 2026-09-14 : **+51 217** on both clients
  (party-shared), 2 793 526 -> 2 844 743 for Tetsouo.
- **Bosses** come from the shard handed over, never from the payout amount : the letter names the boss. #A-#D are the
  upstairs bosses, #E-#H the basement ones. Killing an upstairs boss hands out the matching basement shard in the SAME
  second (#D -> #H, #C -> #G, #B -> #F, #A -> #E) as a 7236 of its own, so the shards in hand are read, not inferred.
- **Items** are the one message that COUNTS rather than sets a bit, so its duplicate is dropped explicitly (same item and
  params within 3 s). Two drops of the same item in the same second are indistinguishable from it and count once.
  Checked : Old Case 0 -> 7 and Ra'Kaz. Sapphire 0 -> 2 in the inventory stacks of the same tape.
- **Not key items.** The 0x055 tables gained nothing during the run (only the map of Outer Ra'Kaznar [U], KI 2305,
  removed at exit). Keys, Plates, Sheets #A-#D and Frag. #1-#4 are handed out at entry as TEMPORARY items (bag 3).

## Payout amounts (the owner's key, 2026-09-15)

| gallimaufry | source | box row |
|---|---|---|
| 10 000 | a basement boss | (bosses come from the shards) |
| 2 000 | an upstairs boss | (bosses come from the shards) |
| 480 | the basement mid NM | NM |
| 300 | a basement coffer | coffers down |
| 100 | an upstairs coffer | coffers up |
| 33 - 90 | kills | - |

Counted on a NEW total only. A change of these amounts by a game update would mis-sort the coffer row ; the
gallimaufry total and the bosses would stay right.

## The id that moves

The payout goes through the same `MsgHealer` as Odyssey's segments and Limbus's units (see
[zone-tracker.md](zone-tracker.md#the-message-id-that-moves)) : seeded 7238, and while that id stays silent the id
whose total rises by exactly its gain on a second message is adopted. The three other messages are read at their
offsets from **the id in use**, not from the seed : a client update renumbers a zone's dialog table as a block
(Odyssey moved 40005 -> 40015 -> 40016 -> 40017 whole), so the payout that re-proves itself carries them along. The
test replays the recorded run with every id +10 and expects the same box. `//aio doctor` prints the state on the
`msgid : Sortie (gallimaufry)` line, and the watcher raises `ZT.MSGID_SILENT` when the zone talks and the id does not.

## The box and its settings

Header "Sortie" ; `Gallimaufry +N · total` ("(last run)" once frozen) ; the eight boss letters A B C D   E F G H
(green = down, orange = its shard in hand, dim = not yet) ; `Coffers N up · N down · NM N` ; one row per item obtained.
Toggles `ztsortie=gallimaufry,bosses,coffers,items` ; text elements `ZT_SO_GAL`, `ZT_SO_BOSS`, `ZT_SO_LINE` ;
preview variant 6.

## Tests

`tests/fixtures_sortie_run.h` holds every 0x02A of the recorded run (generated from the tape, duplicates included).
`tests/t_packets.cpp` replays it and expects exactly the figures above, then checks the shards and the frozen last
run, then replays it renumbered. Six mutations in `dev/scripts/unit_mutate.py` (the item duplicate kept, a coffer
counted per copy, the run summed, a shard kept after its boss, the freeze dropped, the offsets taken from the seed)
each fail the section they target.

## See also
- [Zone tracker](zone-tracker.md) — the six other modes and the shared message healer.
