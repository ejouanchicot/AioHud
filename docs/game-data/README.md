# Game data: player memory & party packets

Until host `ffxi[7]` is walked, AioHUD sources live data two ways: the **local player** from memory (always present + accurate) and the **other party members** from inbound packets (the game never sends you your *own* party packet). Code: `model/game_mem.cpp` (memory), `model/party_state.cpp` (packets).

- [Local player struct](player-struct.md) — pointer chain + field offsets for vitals, name, jobs, and buffs read from memory.
- [Party member packets](party-packets.md) — inbound `0x0DD`/`0x0DF` layouts for other members, plus the alliance-struct alt.
- [Two traps that cost real time](traps.md) — the dangling self-name pointer and the job-ID-vs-LEVEL offset, with the `//aio` debug recipe.
- [The party member ARRAY in memory](party-array.md) — the Ashita `partymember_t` table (18 slots) for an instant full party at load, plus `allianceinfo_t` leadership.
- [Target & SUB-target struct](target-substruct.md) — the heap `target_t` (main vs sub reticle) driving the target bars, the `+0x5C` lock-on flag, plus the party-window picker cursor.
- [Party cast bar — the 0x028 action packet](cast-bar.md) — bit-packed action parse for casting bars, with the don't-clear-on-cat-4 gotcha.
- [Action-menu info box](action-menu.md) — zero-tap Magic / Job Ability / Weapon Skill readout with recast tables and the ghost-menu fix.
- [Party-member buffs — the 0x076 packet](member-buffs.md) — self buffs from memory, other members from the packed `0x076` packet, and the buff atlas.
